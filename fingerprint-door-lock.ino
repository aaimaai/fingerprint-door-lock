#include <Adafruit_Fingerprint.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

#define LOCK_OPEN 7
#define BEEPER 8
#define ROTARY_S 2
#define ROTARY_A 3
#define ROTARY_B 6

#define SPACES "                "
#define MEM0 "_______________________________"
#define DOOR_TIMER 3000
#define TIME_FOR_BED 4000
#define FAKE_LCD false

LiquidCrystal_I2C lcd(0x27, 16, 2);  // Set the LCD I2C address

// For UNO and others without hardware serial, we must use software serial...
// pin #4 is IN from sensor (YELLOW wire)
// pin #5 is OUT from arduino  (WHITE wire)
// comment these two lines if using hardware serial
SoftwareSerial mySerial(4, 5);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

volatile bool clicked = false;
volatile bool click_done = true;
volatile bool show_cursor = 0;
volatile int8_t rotation = 0;
volatile unsigned long last_action = millis();
volatile unsigned long click_timer = 0;

unsigned long lock_open_timer = 0;

bool cursor = false;
bool backlight = true;
bool beeper = false;

int8_t menu_pos = 0;
uint8_t ascii_pos = 0;
char new_user_name[] = SPACES;
int8_t user_id = 0;
char new_user_letter;

char users[15][17];
uint8_t user_ids[15];
int8_t user_count = -1;
byte delete_click;

const byte STATE_IDLE = 0;
const byte STATE_MAIN_MENU = 1;
const byte STATE_ADD_USER = 2;
const byte STATE_ADD_USER_MENU = 3;
const byte STATE_ADD_EXTRA_FINGER = 4;
const byte STATE_DELETE_USER = 5;
const byte STATE_CONFIRM_DELETE_USER = 6;
const byte STATE_DELETE_ALL = 7;

uint8_t current_state = STATE_IDLE;

void setup() {
  Serial.begin(9600);
  Serial.println(F("\n\nAdafruit finger detect test"));
  // set the data rate for the sensor serial port
  finger.begin(57600);
  if (finger.verifyPassword()) {
    Serial.println(F("Found fingerprint sensor!"));
  } else {
    Serial.println(F("Did not find fingerprint sensor :("));
    while (1) {
      delay(1000);
    }
  }

  char init_check[33];
  finger.readNotepad(0, init_check);
  init_check[32] = '\0';
  if (strcmp(init_check, MEM0) != 0) {
    delete_all();
  }

  finger.getTemplateCount();
  Serial.print(F("Sensor contains "));
  Serial.print(finger.templateCount);
  Serial.println(F(" templates"));

  Serial.println(F("Waiting for valid finger..."));

  pinMode(LOCK_OPEN, OUTPUT);
  pinMode(BEEPER, OUTPUT);
  pinMode(ROTARY_A, INPUT_PULLUP);
  pinMode(ROTARY_B, INPUT_PULLUP);
  pinMode(ROTARY_S, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(ROTARY_A), rotate, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ROTARY_S), click, CHANGE);

  if (!FAKE_LCD) {
    lcd.init();  // initialize the lcd
    // Print a message to the LCD.
    lcd.backlight();
  }
  set_display();
}

void loop() {
  unsigned long current_millis = millis();
  int lockOpenTime = current_millis - lock_open_timer;
  if (lock_open_timer > 0 && lockOpenTime >= DOOR_TIMER) {
    digitalWrite(LOCK_OPEN, LOW);
    digitalWrite(BEEPER, LOW);
    lock_open_timer = 0;
    beeper = false;
  }

  if (lock_open_timer > 0) {
    if (lockOpenTime < 500 && (lockOpenTime / 50) % 3 == 0) {
      if (!beeper) {
        digitalWrite(BEEPER, HIGH);
        beeper = true;
      }
    } else {
      if (beeper) {
        digitalWrite(BEEPER, LOW);
        beeper = false;
      }
    }
  }

  if (!FAKE_LCD && show_cursor && ((cursor && current_millis % 1600 > 800) || (!cursor && current_millis % 1600 <= 800))) {
    cursor = !cursor;
    int pos = 0;
    if (current_state == STATE_ADD_USER) {
      pos = 1;
    }
    lcdSetCursor(ascii_pos, pos);
    if (cursor) {
      lcd.cursor();
    } else {
      lcd.noCursor();
    }
  }

  if (rotation != 0) {
    handle_rotation();
  }

  if (clicked) {
    handle_click();
  }

  if (click_timer > 0 && millis() - click_timer > 1000) {
    click_timer = 0;
    handle_long_click();
  }

  long time_idle = current_millis - last_action;
  if (lock_open_timer == 0 && current_state == STATE_IDLE) {
    if (!FAKE_LCD && backlight && time_idle > 10000) {
      lcd.noBacklight();
      backlight = false;
    }
    getFingerprintIDez();
    if (lock_open_timer == 0) {
      delay(1000);
    }
  } else {
    if (!FAKE_LCD && !backlight) {
      lcd.backlight();
      backlight = true;
    }
    if ((current_state == STATE_MAIN_MENU && time_idle > 5000) || time_idle > 30000) {
      current_state = STATE_IDLE;
      set_display();
    }
    delay(10);
  }
}

void get_users() {
  if (user_count > -1) {
    return;
  }
  user_count = 0;
  for (uint8_t i = 1; i < 16; i++) {
    char user_record[33];
    if (finger.readNotepad(i, user_record) == FINGERPRINT_OK && user_record[0] != '\0') {
      char name[17];
      for (uint8_t j = 0; j < 16; j++) {
        name[j] = user_record[j];
      }
      name[16] = '\0';
      strcpy(users[i - 1], name);
      user_ids[user_count] = i - 1;
      user_count++;
    }
  }
  Serial.print(F("Users: "));
  Serial.println(user_count);
}

void click() {
  volatile unsigned long now = millis();
  if (digitalRead(ROTARY_S) == LOW) {
    // prevent weird behaviour when releasing push button
    if (!click_done || now - last_action < 100) {
      return;
    }
    click_timer = millis();
    click_done = false;
  } else {
    if (click_timer > 0) {
      clicked = true;
    }
    click_timer = 0;
    click_done = true;
  }
  last_action = now;
}

void handle_click() {
  clicked = false;
  switch (current_state) {
    case STATE_IDLE:
      current_state = STATE_MAIN_MENU;
      menu_pos = 0;
      break;
    case STATE_MAIN_MENU:
      switch (menu_pos) {
        case 0:  // new user
          current_state = STATE_ADD_USER;
          show_cursor = true;
          menu_pos = 0;
          break;
        case 1:  // delete user
          get_users();
          if (user_count > 0) {
            current_state = STATE_DELETE_USER;
          } else {
            current_state = STATE_MAIN_MENU;
          }
          menu_pos = 0;
          break;
        case 2:  // delete all
          get_users();
          current_state = STATE_DELETE_ALL;
          menu_pos = 0;
          delete_click = 5;
          break;
        default:
          current_state = STATE_IDLE;
      }
      break;
    case STATE_ADD_USER:
      new_user_name[ascii_pos] = new_user_letter;
      ascii_pos++;
      break;
    case STATE_ADD_USER_MENU:
      switch (menu_pos) {
        case 0:  // ok
          if (ascii_pos == 0) {
            new_user_name[ascii_pos] = new_user_letter;
            ascii_pos++;
          }
          enroll_finger();
          current_state = STATE_ADD_EXTRA_FINGER;
          break;
        case 1:  // edit
          current_state = STATE_ADD_USER;
          break;
        case 2:  // backspace
          if (ascii_pos > 0) {
            new_user_name[ascii_pos - 1] = '\0';
            ascii_pos--;
          }
          break;
        case 3:  // annuleren
          current_state = STATE_MAIN_MENU;
          break;
        default:
          current_state = STATE_IDLE;
      }
      break;
    case STATE_ADD_EXTRA_FINGER:
      switch (menu_pos) {
        case 0:
          enroll_finger();
          break;
        case 1:
          current_state = STATE_MAIN_MENU;
          user_count = -1;
          new_user_letter = 0;
          strcpy(new_user_name, SPACES);
          ascii_pos = 0;
          break;
      }
      break;
    case STATE_DELETE_USER:
      user_id = menu_pos;
      current_state = STATE_CONFIRM_DELETE_USER;
      menu_pos = 0;
      break;
    case STATE_CONFIRM_DELETE_USER:
      switch (menu_pos) {
        case 0:
          delete_user(user_id);
          break;
        case 1:
          menu_pos = 0;
          break;
      }
      current_state = STATE_MAIN_MENU;
      break;
    case STATE_DELETE_ALL:
      switch (menu_pos) {
        case 0:
          delete_click--;
          if (delete_click == 0) {
            delete_all();
            current_state = STATE_MAIN_MENU;
          }
          break;
        case 1:
          current_state = STATE_MAIN_MENU;
          break;
      }
      break;
  }
  if (current_state != STATE_ADD_USER_MENU && current_state != STATE_ADD_USER) {
    show_cursor = false;
  }
  set_display();
}

void handle_long_click() {
  switch (current_state) {
    case STATE_MAIN_MENU:
      current_state = STATE_IDLE;
      break;
    case STATE_ADD_USER:
      current_state = STATE_ADD_USER_MENU;
      break;
    default:
      current_state = STATE_MAIN_MENU;
      break;
  }
  if (current_state != STATE_ADD_USER_MENU) {
    show_cursor = false;
  }
  menu_pos = 0;
  if (!FAKE_LCD) {
    lcd.clear();
  }
  set_display();
}

void rotate() {
  unsigned long current_action = millis();
  if (current_action - last_action < 100) {
    return;
  }
  last_action = current_action;
  if (digitalRead(ROTARY_B) != digitalRead(ROTARY_A)) {
    rotation--;
  } else {
    rotation++;
  }
}

void handle_rotation() {
  if (current_state == STATE_IDLE) {
    current_state = STATE_MAIN_MENU;
    rotation = 0;
  }
  set_display();
}

void lcdPrint(const char *line) {
  if (FAKE_LCD) {
    Serial.println(line);
  } else {
    lcd.print(line);
  }
}

void lcdPrint(const __FlashStringHelper *line) {
  if (FAKE_LCD) {
    Serial.println(line);
  } else {
    lcd.print(line);
  }
}

void lcdSetCursor(int x, int y) {
  if (FAKE_LCD) {

  } else {
    lcd.setCursor(x, y);
  }
}

void set_display() {
  switch (current_state) {
    case STATE_IDLE:
      lcdSetCursor(0, 0);
      lcdPrint(F("Fingerscanner   "));
      lcdSetCursor(0, 1);
      lcdPrint(F("ready!          "));
      break;
    case STATE_MAIN_MENU:
      display_main_menu();
      break;
    case STATE_ADD_USER:
      display_add_user();
      break;
    case STATE_ADD_USER_MENU:
      display_add_userMenu();
      break;
    case STATE_ADD_EXTRA_FINGER:
      display_add_finger();
      break;
    case STATE_DELETE_USER:
      display_delete_user();
      break;
    case STATE_CONFIRM_DELETE_USER:
      display_confirm_delete_user();
      break;
    case STATE_DELETE_ALL:
      display_delete_all();
      break;
  }
}

void display_main_menu() {
  const char *MAIN_MENU_ITEMS[] = {"New user", "Delete user", "Delete all", "Cancel"};
  menu_pos = (menu_pos + rotation) % 4;
  if (menu_pos < 0) {
    menu_pos += 4;
  }
  lcdSetCursor(0, 0);
  lcdPrint(F("===== MENU ====="));
  lcdSetCursor(0, 1);
  char menu_item[33] = "> ";
  strcat(menu_item, MAIN_MENU_ITEMS[menu_pos]);
  strcat(menu_item, SPACES);
  menu_item[16] = '\0';
  lcdPrint(menu_item);
  rotation = 0;
}

void display_add_user() {
  int add = 0;
  if (ascii_pos == 0) {
    add = 65;
  } else {
    add = 97;
  }
  menu_pos = (menu_pos + rotation) % 26;
  if (menu_pos < 0) {
    menu_pos += 26;
  }
  lcdSetCursor(0, 0);
  lcdPrint(F("=== ADD USER ==="));
  lcdSetCursor(0, 1);

  char menu_item[33];
  strcpy(menu_item, new_user_name);
  strcat(menu_item, SPACES);
  menu_item[16] = '\0';
  new_user_letter = (char)menu_pos + add;
  menu_item[ascii_pos] = new_user_letter;
  lcdPrint(menu_item);
  rotation = 0;
}

void display_add_userMenu() {
  const char *ADD_USER_MENU_ITEMS[] = {"OK", "Edit name", "Backspace", "Cancel"};
  menu_pos = (menu_pos + rotation) % 4;
  if (menu_pos < 0) {
    menu_pos += 4;
  }
  char menu_item[33];
  strcpy(menu_item, new_user_name);
  strcat(menu_item, SPACES);
  menu_item[16] = '\0';
  lcdSetCursor(0, 0);
  lcdPrint(menu_item);
  lcdSetCursor(0, 1);
  strcpy(menu_item, "> ");
  strcat(menu_item, ADD_USER_MENU_ITEMS[menu_pos]);
  strcat(menu_item, SPACES);
  menu_item[16] = '\0';
  lcdPrint(menu_item);
  rotation = 0;
}

void display_add_finger() {
  if (rotation != 0) {
    menu_pos = menu_pos == 0 ? 1 : 0;
  }
  lcdSetCursor(0, 0);
  lcdPrint(F("ANOTHER FINGER?"));
  lcdSetCursor(0, 1);
  char menu_item[33] = "> ";
  if (!menu_pos) {
    strcat(menu_item, "Yes");
  } else {
    strcat(menu_item, "No");
  }
  strcat(menu_item, SPACES);
  menu_item[16] = '\0';
  lcdPrint(menu_item);
  rotation = 0;
}

void display_delete_user() {
  menu_pos = (menu_pos + rotation) % user_count;
  if (menu_pos < 0) {
    menu_pos += user_count;
  }
  lcdSetCursor(0, 0);
  lcdPrint(F("= DELETE USER  ="));
  lcdSetCursor(0, 1);
  char menu_item[17] = "> ";
  users[user_ids[menu_pos]][16] = '\0';
  strcat(menu_item, users[user_ids[menu_pos]]);
  strcat(menu_item, SPACES);
  menu_item[16] = '\0';
  lcdPrint(menu_item);
  rotation = 0;
}

void display_confirm_delete_user() {
  if (rotation != 0) {
    menu_pos = menu_pos == 0 ? 1 : 0;
  }
  lcdSetCursor(0, 0);
  lcdPrint(F("DELETE USER?    "));
  lcdSetCursor(0, 1);
  char menu_item[33] = "> ";
  if (menu_pos == 0) {
    strcat(menu_item, "Yes");
  } else {
    strcat(menu_item, "No");
  }
  strcat(menu_item, SPACES);
  menu_item[16] = '\0';
  lcdPrint(menu_item);
  rotation = 0;
}

void display_delete_all() {
  if (rotation != 0) {
    menu_pos = menu_pos == 0 ? 1 : 0;
  }
  lcdSetCursor(0, 0);
  lcdPrint(F("= DELETE ALL?? ="));
  lcdSetCursor(0, 1);
  char menu_item[33] = "> ";
  if (menu_pos == 0) {
    strcat(menu_item, "Yes (press 5x)");
  } else {
    strcat(menu_item, "No");
  }
  strcat(menu_item, SPACES);
  menu_item[16] = '\0';
  lcdPrint(menu_item);
  rotation = 0;
}

void delete_user(uint8_t id) {
  char user_record[33];
  uint8_t user_id = user_ids[id] + 1;
  finger.readNotepad(user_id, user_record);
  for (uint8_t i = 16; i < 32; i++) {
    uint8_t model_id = (uint8_t)user_record[i];
    if (model_id > 0) {
      Serial.print(F("Deleting finger # "));
      Serial.print(model_id);
      Serial.print(F(" -- "));
      Serial.println((0x00 << 8) | model_id - 1);
      finger.deleteModel((uint16_t)((0x00 << 8) | model_id - 1));  // model_id back to zero based
    }
  }
  for (uint8_t i = 0; i < 32; i++) {
    user_record[i] = '\0';
  }
  finger.writeNotepad(user_id, user_record);
  user_count = -1;
}

void delete_all() {
  finger.emptyDatabase();
  char mem0[] = MEM0;
  char user_record[32];
  for (uint8_t i = 0; i < 32; i++) {
    user_record[i] = '\0';
  }
  for (uint8_t i = 1; i < 16; i++) {
    finger.writeNotepad(i, user_record);
  }
  finger.writeNotepad(0, mem0);
  user_count = -1;
}

// returns -1 if failed, otherwise returns ID #
int getFingerprintIDez() {
  uint8_t p = finger.getImage();

  if (p != FINGERPRINT_OK)
    return -1;
  p = finger.image2Tz();

  if (p != FINGERPRINT_OK)
    return -1;
  p = finger.fingerSearch();

  if (p != FINGERPRINT_OK) {
    return -1;
  }

  // found a match!
  Serial.print(F("Found ID #"));
  Serial.print(finger.fingerID);
  Serial.print(F(" with confidence of "));
  Serial.println(finger.confidence);

  // don't accept too low confidence
  if (finger.confidence < 35) {
    return -1;
  }

  digitalWrite(LOCK_OPEN, HIGH);
  digitalWrite(BEEPER, HIGH);
  beeper = true;
  lock_open_timer = millis();
  last_action = lock_open_timer;
  return finger.fingerID;
}

void set_new_user_id() {
  if (user_id > 0) {
    return;
  }
  for (uint8_t i = 0; i < 15; i++) {
    if (users[i][0] == 0) {
      user_id = i + 1;
      return;
    }
  }
}

int8_t enroll_finger() {
  get_users();
  set_new_user_id();

  if (user_id == 0) {
    return -1;
  }
  uint8_t finger_id;
  if (scan_finger(finger_id) != 0) {
    return -1;
  }

  char user_record[33];
  finger.readNotepad(user_id, user_record);
  for (uint8_t i = 0; i < 32; i++) {
    if (i < 16) {
      user_record[i] = new_user_name[i];
    } else if (user_record[i] == '\0') {
      user_record[i] = finger_id + 1;  // store it one based
      break;
    }
  }
  finger.writeNotepad(user_id, user_record);
  return 0;
}

int8_t scan_finger(uint8_t &finger_id) {
  uint16_t id;
  finger.getFreeIndex(&id);
  int8_t p = -1;
  lcdSetCursor(0, 0);
  lcdPrint(F("Please scan your"));
  lcdSetCursor(0, 1);
  lcdPrint(F("finger (twice).."));
  Serial.print(F("Waiting for valid finger to enroll as #"));
  Serial.println(id);
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    switch (p) {
      case FINGERPRINT_OK:
        // Serial.println("Image taken");
        break;
      case FINGERPRINT_NOFINGER:
        break;
      case FINGERPRINT_PACKETRECIEVEERR:
        // Serial.println("Communication error");
        break;
      case FINGERPRINT_IMAGEFAIL:
        // Serial.println("Imaging error");
        break;
      default:
        // Serial.println("Unknown error");
        break;
    }
  }

  // OK success!
  p = finger.image2Tz(1);
  switch (p) {
    case FINGERPRINT_OK:
      // Serial.println("Image converted");
      break;
    case FINGERPRINT_IMAGEMESS:
      // Serial.println("Image too messy");
      return p;
    case FINGERPRINT_PACKETRECIEVEERR:
      // Serial.println("Communication error");
      return p;
    case FINGERPRINT_FEATUREFAIL:
      // Serial.println("Could not find fingerprint features");
      return p;
    case FINGERPRINT_INVALIDIMAGE:
      // Serial.println("Could not find fingerprint features");
      return p;
    default:
      // Serial.println("Unknown error");
      return p;
  }

  lcdSetCursor(0, 0);
  lcdPrint(F("Remove finger..."));
  lcdSetCursor(0, 1);
  lcdPrint(F(SPACES));

  for (p = 0; p < 3; p++) {
    delay(100);
    digitalWrite(BEEPER, HIGH);
    delay(100);
    digitalWrite(BEEPER, LOW);
  }
  p = 0;
  while (p != FINGERPRINT_NOFINGER) {
    p = finger.getImage();
  }
  p = -1;
  Serial.println(F("Place same finger again"));

  lcdSetCursor(0, 0);
  lcdPrint(F("Place same      "));
  lcdSetCursor(0, 1);
  lcdPrint(F("finger again... "));

  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    switch (p) {
      case FINGERPRINT_OK:
        // Serial.println("Image taken");
        break;
      case FINGERPRINT_NOFINGER:
        break;
      case FINGERPRINT_PACKETRECIEVEERR:
        // Serial.println("Communication error");
        break;
      case FINGERPRINT_IMAGEFAIL:
        // Serial.println("Imaging error");
        break;
      default:
        // Serial.println("Unknown error");
        break;
    }
  }

  // OK success!

  p = finger.image2Tz(2);
  switch (p) {
    case FINGERPRINT_OK:
      // Serial.println("Image converted");
      break;
    case FINGERPRINT_IMAGEMESS:
      // Serial.println("Image too messy");
      return p;
    case FINGERPRINT_PACKETRECIEVEERR:
      // Serial.println("Communication error");
      return p;
    case FINGERPRINT_FEATUREFAIL:
      // Serial.println("Could not find fingerprint features");
      return p;
    case FINGERPRINT_INVALIDIMAGE:
      // Serial.println("Could not find fingerprint features");
      return p;
    default:
      // Serial.println("Unknown error");
      return p;
  }

  for (p = 0; p < 3; p++) {
    delay(100);
    digitalWrite(BEEPER, HIGH);
    delay(100);
    digitalWrite(BEEPER, LOW);
  }

  // OK converted!

  p = finger.createModel();
  if (p == FINGERPRINT_OK) {
    // Serial.println("Prints matched!");
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    // Serial.println("Communication error");
    return p;
  } else if (p == FINGERPRINT_ENROLLMISMATCH) {
    lcdSetCursor(0, 0);
    lcdPrint(F("Fingerprints did"));
    lcdSetCursor(0, 1);
    lcdPrint(F("not match...    "));
    digitalWrite(BEEPER, HIGH);
    delay(2000);
    digitalWrite(BEEPER, LOW);
    return p;
  } else {
    // Serial.println("Unknown error");
    return p;
  }

  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK) {
    // Serial.println("Stored!");
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    // Serial.println("Communication error");
    return p;
  } else if (p == FINGERPRINT_BADLOCATION) {
    // Serial.println("Could not store in that location");
    return p;
  } else if (p == FINGERPRINT_FLASHERR) {
    // Serial.println("Error writing to flash");
    return p;
  } else {
    // Serial.println("Unknown error");
    return p;
  }
  finger_id = (uint8_t)id;
  return 0;
}
