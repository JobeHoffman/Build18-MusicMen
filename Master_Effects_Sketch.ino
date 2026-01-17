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
const float SMOOTHING = 0.3;
float envelope = 0.0;
float attack = 0.01;
float release = 0.1;

// Update interval
unsigned long lastUpdate = 0;
const int UPDATE_INTERVAL = 10;

void setup() {
  Serial.begin(9600);
  
  AudioMemory(20);

  // Setting Pins Encoder
  pinMode(0, INPUT_PULLUP);
  pinMode(1, INPUT_PULLUP);

  // Enable audio shield
  audioShield.enable();
  audioShield.inputSelect(AUDIO_INPUT_LINEIN);
  audioShield.volume(0.7);
  audioShield.lineInLevel(5);
  audioShield.lineOutLevel(13);
  
  // Initialize filter for auto-wah
  filter.frequency(500);
  filter.resonance(2.0);
  filter.octaveControl(1.0);
  
  // Initialize chorus
  chorus.voices(2);
  
  // Initialize freeverb
  freeverb.roomsize(0.5);
  freeverb.damping(0.5);
  
  // Initialize all effects mixers to off
  for (int i = 0; i < 4; i++) {
    mixer_effects.gain(i, 0);
  }
  
  // Initialize dry/wet mixer
  mixer_drywet.gain(0, 0.5);  // Dry
  mixer_drywet.gain(1, 0.5);  // Wet
  
  // Set initial effect
  setEffect(AUTOWAH);
  
  Serial.println("Multi-Effect Pedal Ready");
  Serial.println("Effect: Auto-Wah");
}

// Potentiometer pins
const int POT1 = 15;
const int POT2 = 16;
const int POT3 = 17;

// Rotary encoder
Encoder myEncoder(0, 1);
long encoderPos = 0;
long lastEncoderPos = 0;

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
  // Higher sensitivity = faster attack/release
  attack = 0.005 + (sensitivity * 0.095);   // 0.005 to 0.1
  release = 0.05 + (sensitivity * 0.45);    // 0.05 to 0.5
  
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
  
  // Smooth frequency changes (reduced smoothing for faster response)
  currentFreq = (SMOOTHING * currentFreq) + ((1.0 - SMOOTHING) * targetFreq);
  filter.frequency(currentFreq);
  
  // Update Q (resonance): 0.7 to 5.0
  float q = 0.7 + (qValue * 4.3);
  filter.resonance(q);
  
  // Update dry/wet mix
  mixer_drywet.gain(0, 1.0 - mixValue);  // Dry
  mixer_drywet.gain(1, mixValue);         // Wet
}

void updateChorus(float depth, float rate, float mixValue) {
  // Depth controls number of voices (1-4)
  int numVoices = 1 + (int)(depth * 3);
  chorus.voices(numVoices);
  
  // Dry/wet mix
  mixer_drywet.gain(0, 1.0 - mixValue);  // Dry
  mixer_drywet.gain(1, mixValue);         // Wet
}

void updateReverb(float roomsize, float damping, float mixValue) {
  // Roomsize: 0.0 to 1.0
  freeverb.roomsize(roomsize);
  
  // Damping: 0.0 to 1.0
  freeverb.damping(damping);
  
  // Dry/wet mix
  mixer_drywet.gain(0, 1.0 - mixValue);  // Dry
  mixer_drywet.gain(1, mixValue);         // Wet
}

void updateDelay(float delayTime, float feedback, float mixValue) {
  int delayMs = (int)(delayTime * 500);
  delay_effect.delay(0, delayMs);
  
  // Feedback: 0.0 to 0.9 (values too high cause runaway feedback)
  float feedbackAmt = feedback * 0.9;

  
  // Dry/wet mix
  mixer_drywet.gain(0, 1.0 - mixValue);  // Dry
  mixer_drywet.gain(1, mixValue);         // Wet
}
