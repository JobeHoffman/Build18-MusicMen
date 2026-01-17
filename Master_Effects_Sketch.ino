#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include <Encoder.h>

// Audio components
AudioInputI2S            i2s_in;
AudioAnalyzePeak         peak;
AudioFilterStateVariable filter;
AudioEffectChorus        chorus;
AudioEffectFreeverb      freeverb;
AudioEffectDelay         delay_effect;
AudioMixer4              mixer_effects;  // Mix between effects
AudioMixer4              mixer_drywet;   // Final dry/wet mix
AudioOutputI2S           i2s_out;
AudioControlSGTL5000     audioShield;

// Audio connections
AudioConnection patchCord1(i2s_in, 0, peak, 0);              // Input to peak analyzer (for auto-wah)
AudioConnection patchCord2(i2s_in, 0, filter, 0);            // Input to filter (auto-wah)
AudioConnection patchCord3(i2s_in, 0, chorus, 0);            // Input to chorus
AudioConnection patchCord4(i2s_in, 0, freeverb, 0);          // Input to freeverb
AudioConnection patchCord5(i2s_in, 0, delay_effect, 0);      // Input to delay
AudioConnection patchCord6(filter, 0, mixer_effects, 0);     // Auto-wah to effects mixer
AudioConnection patchCord7(chorus, 0, mixer_effects, 1);     // Chorus to effects mixer
AudioConnection patchCord8(freeverb, 0, mixer_effects, 2);   // Freeverb to effects mixer
AudioConnection patchCord9(delay_effect, 0, mixer_effects, 3); // Delay to effects mixer
AudioConnection patchCord10(i2s_in, 0, mixer_drywet, 0);     // Dry signal
AudioConnection patchCord11(mixer_effects, 0, mixer_drywet, 1); // Wet signal
AudioConnection patchCord12(mixer_drywet, 0, i2s_out, 0);    // Output left
AudioConnection patchCord13(mixer_drywet, 0, i2s_out, 1);    // Output right


// Setting Pins Encoder
pinMode(0, INPUT_PULLUP);
pinMode(1, INPUT_PULLUP);

// Rotary encoder
Encoder myEncoder(D1, D2); // rotary encoder code.            
long encoderPos = 0;
long lastEncoderPos = 0;

// Setting Pins Potentiometers
pinMode(15, INPUT);
pinMode(16, INPUT);
pinMode(17, INPUT);

// Potentiometer pins
const int POT1 = A1;
const int POT2 = A2;
const int POT3 = A3;

// Effect selection
enum Effect {
  AUTOWAH = 0,
  CHORUS = 1,
  REVERB = 2,
  DELAY = 3,
  NUM_EFFECTS = 4
};

Effect currentEffect = AUTOWAH;

// Auto-wah parameters
float currentFreq = 500.0;
float targetFreq = 500.0;
const float MIN_FREQ = 200.0;
const float MAX_FREQ = 2500.0;
const float SMOOTHING = 0.85;
float envelope = 0.0;
float attack = 0.01;
float release = 0.1;

// Update interval
unsigned long lastUpdate = 0;
const int UPDATE_INTERVAL = 10;

void setup() {
  Serial.begin(9600);
  
  AudioMemory(20);
  
  // Enable audio shield
  audioShield.enable();
  audioShield.inputSelect(MYINPUT_LINEIN);
  audioShield.volume(0.7);
  audioShield.lineInLevel(5);
  audioShield.lineOutLevel(13);
  
  // Initialize filter for auto-wah
  filter.frequency(500);
  filter.resonance(2.0);
  filter.octaveControl(1.0);
  
  // Initialize all effects mixers to off
  for (int i = 0; i < 4; i++) {
    mixer_effects.gain(i, 0);
  }
  
  // Set initial effect
  setEffect(AUTOWAH);
  
  Serial.println("Multi-Effect Pedal Ready");
  Serial.println("Effect: Auto-Wah");
}

void loop() {
  unsigned long currentTime = millis();
  
  // Check rotary encoder for effect selection
  encoderPos = myEncoder.read() / 4; // Divide by 4 for detents
  
  if (encoderPos != lastEncoderPos) {
    // Constrain encoder position to number of effects
    if (encoderPos < 0) {
      encoderPos = NUM_EFFECTS - 1;
      myEncoder.write(encoderPos * 4);
    } else if (encoderPos >= NUM_EFFECTS) {
      encoderPos = 0;
      myEncoder.write(0);
    }
    
    currentEffect = (Effect)encoderPos;
    setEffect(currentEffect);
    lastEncoderPos = encoderPos;
    
    // Print current effect
    Serial.print("Effect: ");
    switch(currentEffect) {
      case AUTOWAH: Serial.println("Auto-Wah"); break;
      case CHORUS: Serial.println("Chorus"); break;
      case REVERB: Serial.println("Reverb"); break;
      case DELAY: Serial.println("Delay"); break;
    }
  }
  
  // Update parameters at regular intervals
  if (currentTime - lastUpdate >= UPDATE_INTERVAL) {
    lastUpdate = currentTime;
    
    // Read potentiometers
    float knob1 = analogRead(POT1) / 1023.0;
    float knob2 = analogRead(POT2) / 1023.0;
    float knob3 = analogRead(POT3) / 1023.0;
    
    // Update parameters based on current effect
    switch(currentEffect) {
      case AUTOWAH:
        updateAutoWah(knob1, knob2, knob3);
        break;
      case CHORUS:
        updateChorus(knob1, knob2, knob3);
        break;
      case REVERB:
        updateReverb(knob1, knob2, knob3);
        break;
      case DELAY:
        updateDelay(knob1, knob2, knob3);
        break;
    }
  }
}

void setEffect(Effect effect) {
  // Turn off all effects
  for (int i = 0; i < 4; i++) {
    mixer_effects.gain(i, 0);
  }
  
  // Turn on selected effect
  mixer_effects.gain((int)effect, 1.0);
}

void updateAutoWah(float sensitivity, float qValue, float mixValue) {
  // Update envelope follower parameters based on sensitivity
  attack = 0.005 + (sensitivity * 0.045);
  release = 0.05 + (sensitivity * 0.45);
  
  // Read peak from audio input
  if (peak.available()) {
    float peakValue = peak.read();
    
    // Envelope follower with attack/release
    if (peakValue > envelope) {
      envelope = envelope + (peakValue - envelope) * attack;
    } else {
      envelope = envelope + (peakValue - envelope) * release;
    }
    
    // Map envelope to frequency range with sensitivity scaling
    float envScaled = pow(envelope, 1.0 / (sensitivity + 0.5));
    targetFreq = MIN_FREQ + (envScaled * (MAX_FREQ - MIN_FREQ));
    targetFreq = constrain(targetFreq, MIN_FREQ, MAX_FREQ);
  }
  
  // Smooth frequency changes
  currentFreq = (SMOOTHING * currentFreq) + ((1.0 - SMOOTHING) * targetFreq);
  filter.frequency(currentFreq);
  
  // Update Q (resonance): 0.7 to 5.0
  float q = 0.7 + (qValue * 4.3);
  filter.resonance(q);
  
  // Update dry/wet mix
  mixer_drywet.gain(0, 1.0 - mixValue);  // Dry
  mixer_drywet.gain(1, mixValue);         // Wet
}

void updateChorus(float voices, float toggle, float dryLevel) {
  int voicesVal = static_cast<int>(10 * voices);
  chorus.voices(voicesVal);
  
  // Dry/wet mix
  mixer_drywet.gain(0, dryLevel);  // Dry
  mixer_drywet.gain(1, 0.5);        // Wet
}

void updateReverb(float roomsize, float damping, float dryLevel) {
  freeverb.roomsize(roomsize);
  freeverb.damping(damping);
  
  // Dry/wet mix
  mixer_drywet.gain(0, dryLevel);  // Dry
  mixer_drywet.gain(1, 0.5);        // Wet
}

void updateDelay(float delayTime, float toggle, float dryLevel) {
  int delayVal = (int)(delayTime * 2000);  // 0-2000ms
  delay_effect.delay(0, delayVal);
  
  // Toggle effect on/off
  if (toggle < 0.5) {
    delay_effect.disable(0);
  }
  
  // Dry/wet mix
  mixer_drywet.gain(0, dryLevel);  // Dry
  mixer_drywet.gain(1, 0.5);        // Wet
}[
