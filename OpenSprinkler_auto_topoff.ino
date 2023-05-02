// Input/Output pin variables. Adjust dependent on your set-up. Program requires 3 level switches that are high in air. 
int lshh = 2;       //high high level switch. Indicates an overfill status 
int lsh = 3;        //high level switch. Indicating an ideal level
int lsl = 4;        //low level switch. indicating a fill is needed
int wtrbtn = 5;     //connected to a NO push button. Connected pin -> button -> GND
int os = 6;         //output pin to OpenSprinkler. Normally open un-checked
int wtr_drain = 7;   //output for drain solenoid
int wtr_fill = 8;      //output for filling solenoid
int led_pin = 13;   //output for the alarm LED

// byte value to represent the 3 level switch inputs and their stability
byte level = 0; //do not edit
byte old_level = 0; //do not edit
bool stable = false; //do not edit

// Boolean for pin states. Initially set low to try and eliminate false triggers upon powering on.
bool fill_state = LOW; //do not edit
bool os_state = LOW; //do not edit
bool empty_state = LOW; //do not edit
bool led_state = LOW; //do not edit
bool error_state = LOW; //do not edit
bool wtrbtn_state = LOW; //do not edit

// Timing vars
unsigned long btn_startMillis;  //Millis for button push
unsigned long lv_startMillis;   //Millis for level stability
unsigned long os_startMillis;   //Millis for OpenSprinkler timer
unsigned long led_startMillis;  //Millis for error LED flashing
unsigned long currentMillis;    //self explanatory
unsigned long lv_calc;          //variable for period calculations
unsigned long btn_calc;         //variable for period calculations
unsigned long os_calc;          //variable for period calculations
unsigned long led_calc;         //variable for period calculations

// Timing constants in milliseconds (1 second = 1000 milliseconds)
const unsigned long max_period = 100000;  //With my probes, for some reason, I get a period of over 429496000. This variable is to prevent a false trigger due to this anomaly
const unsigned long lv_period = 20000;    //Time the level probes must be stable for before processing the state change. My case is a swimming pool and swimmers can trigger a fill or empty if this check is not long enough.
const unsigned long btn_period = 1500;    //Length of time needed for triggering OpenSprinkler. OS requires more than 1 second.
const unsigned long os_period_on = 1500;  //On time for continuously triggering as a "program switch". OS requires more than 1 second.
const unsigned long os_period_off = 500;  //Off time for continuously triggering as a "program switch". Slight pause before triggering the on cycle again.
const unsigned long led_period = 250;     //If a level probe malfunctions. This is the time interval for flashing an alarm LED (250 = .25 seconds)


void setup() { //setup only runs at startup

  // Set output pins
  pinMode(wtr_drain, OUTPUT);
  pinMode(wtr_fill, OUTPUT);
  pinMode(os, OUTPUT);
  pinMode(led_pin, OUTPUT);

  // Set input pin
  pinMode(lshh, INPUT_PULLUP);
  pinMode(lsh, INPUT_PULLUP);
  pinMode(lsl, INPUT_PULLUP);
  pinMode(wtrbtn, INPUT_PULLUP);

  //set intal output states
  digitalWrite(wtr_fill, fill_state);
  digitalWrite(wtr_drain, empty_state);
  digitalWrite(os, os_state);
  digitalWrite(led_pin, led_state);
}


void loop() {         //main loop

  old_level = level;  // save old level state
  readpins();         // read current pins states
  lv_stability();     // check if the level switches are stable
  process_levels();   // process what to do based on the level
  os_signal();        // send signal to OpenSprinkler
  blink_led();        // Blink an LED in the event of an error
  waterbtn();         // envoke master valve to use water ate the pool

  digitalWrite(wtr_fill, fill_state);
  digitalWrite(wtr_drain, empty_state);
  digitalWrite(os, os_state);
  digitalWrite(led_pin, led_state);
}


void readpins() {
  level = 0;
  if (digitalRead(lsl) == HIGH)  // b001
  {
    level = level + 1;
  }
  if (digitalRead(lsh) == HIGH)  // b010
  {
    level = level + 2;
  }
  if (digitalRead(lshh) == HIGH)  // b100
  {
    level = level + 4;
  }

  wtrbtn_state = digitalRead(wtrbtn);
}


void lv_stability() {
  if (level == old_level) { //level states have not changed
    if (stable == false) {
      // calculate the time between changes in level value.
      currentMillis = millis();  //get the current "time" (actually the number of milliseconds since the program started)
      lv_calc = currentMillis - lv_startMillis;
      if ((lv_calc < max_period) && (lv_calc > lv_period)) { //calculate the amount of time the the level probes have been stable
        stable = true;
      }
    }
  } else {
    lv_startMillis = millis();  // Change start time
    stable = false;
  }
}


void process_levels() {
  if (stable == true) {
    //  High in air sensors, logic that was used
    //  High    Ideal   Low   Result/Responce
    //  100   |  10   |   1  =  111 (7) Empty/Needs filling
    //  100   |  10   |   0  =  110 (6) Between empty and ideal (decide if container is filling of not)
    //  100   |  0    |   0  =  100 (4) Ideal/Stop filling
    //  0     |  0    |   0  =  000 (0) Full/Needs emptying
    
    switch (level) {
      case 7:  // Start filling
        fill_state = HIGH;
        empty_state = LOW;
        error_state = LOW;
        break;

      case 6:
        if (fill_state == HIGH)  // if filling, continue filling
        {
          fill_state = HIGH;
          empty_state = LOW;
          error_state = LOW;
          break;
        } else if (empty_state == HIGH)  // if draining, stop draining
        {
          fill_state = LOW;
          empty_state = LOW;
          error_state = LOW;
        }
        break;

      case 5:  // enter error state
        fill_state = LOW;
        empty_state = LOW;
        error_state = HIGH;
        break;

      case 4:
        if (fill_state == HIGH)  // if filling, stop filling
        {
          fill_state = LOW;
          empty_state = LOW;
          error_state = LOW;
        } else if (error_state == HIGH)  // if draining, continue draining
        {
          fill_state = LOW;
          empty_state = HIGH;
          error_state = LOW;
        }
        break;

      case 1 ... 3:  // enter error state
        fill_state = LOW;
        empty_state = LOW;
        error_state = HIGH;
        break;

      case 0:  // over full, start draining
        fill_state = LOW;
        empty_state = HIGH;
        error_state = LOW;
        break;
    }
  }
}


void os_signal() {
  // If needs filling send a 1.5 sec on and .5 sec off signal to OS, to trigger the master valve, until the container has been filled
  if (fill_state == HIGH) {
    if (os_state == LOW) {
      currentMillis = millis();
      if (currentMillis - os_startMillis >= os_period_off) {
        os_state = HIGH;
        os_startMillis = currentMillis;
      }
    } else {
      currentMillis = millis();
      if (currentMillis - os_startMillis >= os_period_on) {
        os_state = LOW;
        os_startMillis = currentMillis;
      }
    }
  } else {
    os_state = LOW;
  }
}


void blink_led() { //This is only used if there is an error with the level probes
  if (error_state == HIGH) {
    currentMillis = millis();
    if (currentMillis - led_startMillis >= led_period) {
      led_state = !led_state;
      led_startMillis = currentMillis;
    }
  } else {
    led_state = LOW;
  }
}


void waterbtn() {
  if (fill_state == LOW) { //container is not filling
    if (os_state == LOW) { //OS is not being triggered
      if (wtrbtn_state == LOW) { //button is currently being pushed
        btn_startMillis = millis();
        os_state = HIGH;
      }
    } else if (os_state == HIGH) { //OS is being triggered
      currentMillis = millis();
      btn_calc = currentMillis - btn_startMillis;
      if ((btn_calc > btn_period) && (btn_calc < max_period)) { //check if OS has been triggered long enough or not
        os_state = LOW;
      }
    }
  }
}