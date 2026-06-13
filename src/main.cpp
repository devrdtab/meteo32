#include <Adafruit_AHTX0.h> // Подключает библиотеку для датчика температуры и влажности AHT.
#include <Adafruit_BMP280.h> // Подключает библиотеку для датчика давления BMP280.
#include <Arduino.h> // Подключает базовые функции Arduino: pinMode, digitalWrite, millis, delay и т.д.
#include <LiquidCrystal_I2C.h> // Подключает библиотеку для LCD-дисплея через I2C.
#include <Wire.h>              // Подключает библиотеку для работы с I2C-шиной.

// ================= PINS ================
#define PIN_BTN 10      // Пин, к которому подключена кнопка.
#define PIN_LED_GREEN 5 // Пин зелёного светодиода.
#define PIN_LED_RED 6   // Пин красного светодиода.
#define PIN_BUZZER 13   // Пин зуммера.

// ================= LCD =================
#define LCD_WIDTH 16 // Количество символов в одной строке LCD.
#define LCD_HEIGHT 2 // Количество строк LCD.

// ================= CALIBRATION =========
// Поправка температуры: от измерения отнимается 1.2 градуса.
#define CAL_TEMP_OFFSET -1.2f
// Поправка влажности: от измерения отнимается 1 процент.
#define CAL_HUM_OFFSET -1.0f
// Поправка давления: к измерению добавляется 10.9 hPa.
#define CAL_PRES_OFFSET 10.9f

// ================= THRESHOLDS ==========
#define THRESHOLD_TEMP_MAX 26.0f   // Максимально допустимая температура.
#define THRESHOLD_TEMP_MIN 19.0f   // Минимально допустимая температура.
#define THRESHOLD_HUM_MAX 80.0f    // Максимально допустимая влажность.
#define THRESHOLD_HUM_MIN 20.0f    // Минимально допустимая влажность.
#define THRESHOLD_PRES_MAX 1015.0f // Максимально допустимое давление.
#define THRESHOLD_PRES_MIN 1000.0f // Минимально допустимое давление.

// ================= TIMING ==============
// Как часто читать датчики в обычной работе: 10000 мс = 10 секунд.
#define SENSOR_READ_PERIOD 20000UL
// Сколько экран горит после нажатия кнопки: 5000 мс = 5 секунд.
#define ACTIVE_DISPLAY_TIME 5000UL
// Как часто мигать зелёным LED в ожидании: 7000 мс = 7 секунд.
#define STANDBY_LED_PERIOD 20000UL
// Как долго держать зелёный LED включённым при мигании: 10 мс.
#define STANDBY_LED_PULSE 10UL
// Период переключения красного LED в тревоге: 1000 мс.
#define ALARM_LED_HALF_PERIOD 1000UL
// Как часто запускать серию писков в тревоге: 3000 мс.
#define ALARM_BUZZER_PERIOD 3000UL
// Длительность одного писка: 100 мс.
#define ALARM_BUZZER_PULSE 100UL
// Пауза после отключения тревоги кнопкой: 3600000 мс = 1 час.
#define ALARM_COOLDOWN 3600000UL
// Сколько показывать показания после загрузки: 7000 мс = 7 секунд.
#define STARTUP_DISPLAY_TIME 7000UL

enum AppMode {  // Создаём список возможных режимов работы устройства.
  MODE_STANDBY, // Режим ожидания: экран выключен, зелёный LED иногда
                // мигает.
  MODE_ACTIVE,  // Активный режим: экран включён и показывает показания.
  MODE_ALARM    // Режим тревоги: красный LED мигает, зуммер пищит.
};

// ================= OBJECTS =================
LiquidCrystal_I2C
    lcd(0x27, LCD_WIDTH,
        LCD_HEIGHT); // Создаёт объект LCD: адрес I2C 0x27, размер 16x2.
Adafruit_AHTX0 aht;  // Создаёт объект датчика AHT.
Adafruit_BMP280 bmp; // Создаёт объект датчика BMP280.

// ================= STATE =================
AppMode mode =
    MODE_STANDBY; // Переменная хранит текущий режим работы, сначала standby.

float t = 0, h = 0, p = 0; // Переменные для температуры, влажности и давления.

bool aT =
    false; // Флаг аварии температуры: true, если температура вышла за пределы.
bool aH =
    false; // Флаг аварии влажности: true, если влажность вышла за пределы.
bool aP = false; // Флаг аварии давления: true, если давление вышло за пределы.

unsigned long lastSensor = 0;  // Время последнего чтения датчиков.
unsigned long activeStart = 0; // Время входа в активный режим.

unsigned long greenStart = 0; // Время включения зелёного LED.
unsigned long greenLast = 0;  // Время последнего запуска мигания зелёного LED.
bool greenOn = false;         // Состояние зелёного LED: включён или выключен.

unsigned long redLast = 0; // Время последнего переключения красного LED.
bool redOn = false;        // Состояние красного LED: включён или выключен.

unsigned long buzzLast = 0;  // Время последней серии писков.
unsigned long buzzStart = 0; // Время начала текущего писка или паузы.
bool buzzOn = false;         // Состояние зуммера: пищит или молчит.
int buzzCount = 0;           // Счётчик стадии серии писков.

bool cooldown = false;           // Флаг паузы после отключения тревоги кнопкой.
unsigned long cooldownStart = 0; // Время начала cooldown.

// ================= BUTTON =================
bool lastBtn = HIGH; // Предыдущее сырое состояние кнопки.
bool stableBtn =
    HIGH; // Последнее стабильное состояние кнопки после антидребезга.
unsigned long btnTime = 0; // Время последнего изменения состояния кнопки.

bool buttonPressed() { // Функция возвращает true один раз в момент нажатия
                       // кнопки.
  bool r = digitalRead(PIN_BTN); // Читаем текущее состояние кнопки.
  bool pressed = false;          // По умолчанию считаем, что нажатия не было.

  if (r != lastBtn) {   // Если состояние кнопки изменилось.
    btnTime = millis(); // Запоминаем время изменения.
    lastBtn = r;        // Обновляем предыдущее состояние.
  }

  if (millis() - btnTime > 50) { // Ждём 50 мс, чтобы убрать дребезг контактов.
    if (r == LOW &&
        stableBtn == HIGH) // Если кнопка стала нажатой, а раньше была отпущена.
      pressed = true;      // Фиксируем одно нажатие.

    stableBtn = r; // Обновляем стабильное состояние кнопки.
  }

  return pressed; // Возвращаем результат: было новое нажатие или нет.
}

// ================= DISPLAY =================
void clearLine(int line) { // Функция очищает одну строку LCD.
  lcd.setCursor(0, line);  // Ставим курсор в начало выбранной строки.
  for (int i = 0; i < LCD_WIDTH; i++) { // Проходим по всем символам строки.
    lcd.print(" "); // Печатаем пробел, затирая старый символ.
  }
  lcd.setCursor(0, line); // Возвращаем курсор в начало очищенной строки.
}

void bootAnimation() {        // Стартовая анимация загрузки.
  const int totalSteps = 100; // Всего 100 шагов, от 0% до 100%.
  const int lcdDelay = 10;    // Пауза между шагами анимации: 20 мс.
  int ledState = LOW; // Текущее состояние зелёного LED во время загрузки.
  unsigned long lastBlink = 0; // Время последнего переключения зелёного LED.

  lcd.display();   // Включаем вывод символов на LCD.
  lcd.backlight(); // Включаем подсветку LCD.
  clearLine(0);    // Очищаем первую строку.
  clearLine(1);    // Очищаем вторую строку.

  for (int percent = 0; percent <= totalSteps;
       percent++) { // Цикл от 0 до 100 процентов.
    int charsToFill = (percent * LCD_WIDTH) /
                      100; // Считаем, сколько символов прогресс-бара заполнить.

    lcd.setCursor(0, 0); // Ставим курсор в начало первой строки.
    for (int i = 0; i < LCD_WIDTH;
         i++) { // Проходим по всем позициям первой строки.
      lcd.write(i < charsToFill ? (uint8_t)255
                                : ' '); // Печатаем полный блок или пробел.
    }

    lcd.setCursor(0, 1);    // Ставим курсор в начало второй строки.
    lcd.print("Loading: "); // Печатаем текст загрузки.
    if (percent < 10)       // Если процент однозначный.
      lcd.print(" ");       // Добавляем пробел для ровного выравнивания.
    lcd.print(percent);     // Печатаем число процентов.
    lcd.print("%   "); // Печатаем знак процента и пробелы, чтобы стереть старые
                       // символы.

    int blinkInterval = map(percent, 0, totalSteps, 800,
                            20); // Чем больше процент, тем быстрее мигает LED.
    if (millis() - lastBlink >=
        (unsigned long)blinkInterval) { // Проверяем, пора ли переключить LED.
      ledState = !ledState;             // Инвертируем состояние LED.
      digitalWrite(PIN_LED_GREEN,
                   ledState); // Записываем новое состояние на зелёный LED.
      lastBlink = millis();   // Запоминаем время переключения.
    }

    delay(lcdDelay); // Делаем паузу, чтобы анимация была видимой.
  }

  digitalWrite(PIN_LED_GREEN, LOW); // После загрузки выключаем зелёный LED.
  clearLine(0);                     // Очищаем первую строку.
  clearLine(1);                     // Очищаем вторую строку.
}

void lcdUpdate() { // Функция обновляет показания на LCD.
  lcd.clear();     // Полностью очищает экран.

  lcd.setCursor(0, 0); // Переходит на первую строку.

  lcd.print(aT ? "*"
               : " "); // Если авария температуры, печатает *, иначе пробел.
  lcd.print("T:");     // Печатает подпись температуры.
  lcd.print(t, 1);     // Печатает температуру с одним знаком после запятой.
  lcd.print("C ");     // Печатает единицу измерения температуры.

  lcd.print(aH ? "*" : " "); // Если авария влажности, печатает *, иначе пробел.
  lcd.print("H:");           // Печатает подпись влажности.
  lcd.print((int)h);         // Печатает влажность целым числом.
  lcd.print("%");            // Печатает знак процента.

  lcd.setCursor(0, 1); // Переходит на вторую строку.

  lcd.print(aP ? "*" : " "); // Если авария давления, печатает *, иначе пробел.
  lcd.print("P:");           // Печатает подпись давления.
  lcd.print((int)p);         // Печатает давление целым числом.
  lcd.print("hPa");          // Печатает единицу давления.
}

void lcdOn(bool on) {  // Функция включает или выключает LCD.
  if (on) {            // Если передали true.
    lcd.display();     // Включаем отображение символов.
    lcd.backlight();   // Включаем подсветку.
  } else {             // Если передали false.
    lcd.noDisplay();   // Выключаем отображение символов.
    lcd.noBacklight(); // Выключаем подсветку.
  }
}

// ================= SENSORS =================
void readSensors() {        // Функция читает датчики и проверяет аварии.
  sensors_event_t h_e, t_e; // Создаём структуры для влажности и температуры.

  if (aht.getEvent(&h_e, &t_e)) {          // Читаем AHT; если чтение успешно.
    t = t_e.temperature + CAL_TEMP_OFFSET; // Сохраняем температуру с поправкой.
    h = h_e.relative_humidity +
        CAL_HUM_OFFSET; // Сохраняем влажность с поправкой.
  }

  p = bmp.readPressure() / 100.0f +
      CAL_PRES_OFFSET; // Читаем давление, переводим Па в hPa и добавляем
                       // поправку.

  aT = (t > THRESHOLD_TEMP_MAX ||
        t < THRESHOLD_TEMP_MIN); // Проверяем, вышла ли температура за пределы.
  aH = (h > THRESHOLD_HUM_MAX ||
        h < THRESHOLD_HUM_MIN); // Проверяем, вышла ли влажность за пределы.
  aP = (p > THRESHOLD_PRES_MAX ||
        p < THRESHOLD_PRES_MIN); // Проверяем, вышло ли давление за пределы.

  Serial.printf("T=%.1f H=%.1f P=%.1f\n", t, h,
                p); // Отправляем показания в Serial Monitor.
}

bool alarm() {
  return aT || aH || aP;
} // Возвращает true, если есть хотя бы одна авария.

// ================= MODES =================
void enterStandby() {  // Переводит устройство в режим ожидания.
  mode = MODE_STANDBY; // Устанавливает текущий режим standby.
  lcdOn(false);        // Выключает экран.

  digitalWrite(PIN_LED_RED, LOW); // Выключает красный LED.
  digitalWrite(PIN_BUZZER, LOW);  // Выключает зуммер.

  greenOn = false; // Сбрасывает флаг зелёного LED.
  greenLast =
      millis() -
      STANDBY_LED_PERIOD; // Делает так, чтобы первый зелёный импульс был сразу.
}

void enterActive() {  // Переводит устройство в активный режим.
  mode = MODE_ACTIVE; // Устанавливает текущий режим active.
  lcdOn(true);        // Включает экран.

  readSensors(); // Сразу читает свежие данные с датчиков.
  lcdUpdate();   // Показывает эти данные на LCD.

  activeStart = millis(); // Запоминает время входа в active.
}

void enterAlarm() {  // Переводит устройство в режим тревоги.
  mode = MODE_ALARM; // Устанавливает текущий режим alarm.
  lcdOn(false);      // Выключает экран в тревоге.

  redOn = false;  // Сбрасывает состояние красного LED.
  buzzOn = false; // Сбрасывает состояние зуммера.
  buzzCount = 0;  // Сбрасывает счётчик писков.

  digitalWrite(PIN_LED_RED, LOW); // Выключает красный LED.
  digitalWrite(PIN_BUZZER, LOW);  // Выключает зуммер.
}

// ================= HANDLERS =================
void handleStandby(bool btn) {  // Обрабатывает поведение в режиме standby.
  unsigned long now = millis(); // Запоминает текущее время.

  if (!greenOn &&
      now - greenLast >= STANDBY_LED_PERIOD) { // Если зелёный LED выключен и
                                               // пришло время мигнуть.
    digitalWrite(PIN_LED_GREEN, HIGH);         // Включает зелёный LED.
    greenOn = true;                            // Запоминает, что LED включён.
    greenStart = now;                          // Запоминает время включения.
    greenLast = now; // Запоминает время последнего запуска импульса.
  }

  if (greenOn &&
      now - greenStart >=
          STANDBY_LED_PULSE) { // Если зелёный LED горит достаточно долго.
    digitalWrite(PIN_LED_GREEN, LOW); // Выключает зелёный LED.
    greenOn = false;                  // Запоминает, что LED выключен.
  }

  if (now - lastSensor >
      SENSOR_READ_PERIOD) { // Если пора снова прочитать датчики.
    lastSensor = now;       // Обновляет время последнего чтения.
    readSensors();          // Читает датчики.

    if (cooldown &&
        now - cooldownStart >=
            ALARM_COOLDOWN) { // Если cooldown включён и уже прошёл час.
      cooldown = false; // Выключает cooldown, тревога снова может сработать.
    }

    if (alarm() && !cooldown) { // Если есть авария и cooldown не активен.
      enterAlarm();             // Переходит в режим тревоги.
      return; // Выходит из функции, чтобы дальше standby не выполнялся.
    }
  }

  if (btn)         // Если кнопка была нажата.
    enterActive(); // Переходит в активный режим.
}

void handleActive(bool btn) {   // Обрабатывает поведение в активном режиме.
  unsigned long now = millis(); // Запоминает текущее время.

  if (now - lastSensor > SENSOR_READ_PERIOD) { // Если пора обновить показания.
    lastSensor = now; // Обновляет время последнего чтения.
    readSensors();    // Читает датчики.
    lcdUpdate();      // Обновляет LCD.
  }

  if (btn) {                // Если нажали кнопку в active.
    activeStart = millis(); // Продлевает время active-режима.
    readSensors();          // Читает свежие данные.
    lcdUpdate();            // Сразу обновляет экран.
  }

  if (now - activeStart > ACTIVE_DISPLAY_TIME) { // Если active-режим длится
                                                 // дольше разрешённого времени.
    enterStandby(); // Возвращается в режим ожидания.
  }
}

void handleAlarm(bool btn) {    // Обрабатывает поведение в режиме тревоги.
  unsigned long now = millis(); // Запоминает текущее время.

  if (now - redLast >
      ALARM_LED_HALF_PERIOD) { // Если пора переключить красный LED.
    redOn = !redOn;            // Инвертирует состояние красного LED.
    digitalWrite(PIN_LED_RED,
                 redOn); // Записывает новое состояние на пин красного LED.
    redLast = now;       // Запоминает время переключения.
  }

  if (!buzzOn && buzzCount == 0 &&
      now - buzzLast >
          ALARM_BUZZER_PERIOD) {    // Если пора начать новую серию писков.
    digitalWrite(PIN_BUZZER, HIGH); // Включает зуммер.
    buzzOn = true;                  // Запоминает, что зуммер включён.
    buzzStart = now;                // Запоминает время начала писка.
    buzzCount = 1;                  // Отмечает, что идёт первый писк.
  }

  if (buzzOn &&
      now - buzzStart >
          ALARM_BUZZER_PULSE) {    // Если текущий писк уже достаточно длинный.
    digitalWrite(PIN_BUZZER, LOW); // Выключает зуммер.
    buzzOn = false;                // Запоминает, что зуммер выключен.

    if (buzzCount == 1) { // Если закончился первый писк.
      buzzCount = 2;      // Переходит к паузе перед вторым писком.
      buzzStart = now;    // Запоминает начало паузы.
    } else {              // Если закончился второй писк.
      buzzCount = 0;      // Сбрасывает серию.
      buzzLast = now;     // Запоминает время окончания серии.
    }
  }

  if (!buzzOn && buzzCount == 2 &&
      now - buzzStart >
          ALARM_BUZZER_PULSE) { // Если пауза перед вторым писком закончилась.
    digitalWrite(PIN_BUZZER, HIGH); // Включает второй писк.
    buzzOn = true;                  // Запоминает, что зуммер включён.
    buzzStart = now;                // Запоминает время начала второго писка.
    buzzCount = 3;                  // Отмечает, что второй писк идёт.
  }

  if (btn) { // Если нажали кнопку в тревоге.
    cooldown =
        true; // Включает cooldown, чтобы тревога не включилась сразу снова.
    cooldownStart = now; // Запоминает начало cooldown.
    enterActive();       // Переходит в active и показывает показания.
  }
}

// ================= SETUP =================
void setup() { // setup выполняется один раз при включении или перезагрузке
               // платы.
  Serial.begin(115200); // Запускает Serial Monitor на скорости 115200.

  delay(500); // Небольшая пауза для стабилизации питания и устройств.

  pinMode(PIN_LED_GREEN, OUTPUT); // Настраивает зелёный LED как выход.
  pinMode(PIN_LED_RED, OUTPUT);   // Настраивает красный LED как выход.
  pinMode(PIN_BUZZER, OUTPUT);    // Настраивает зуммер как выход.
  pinMode(PIN_BTN, INPUT_PULLUP); // Настраивает кнопку как вход с внутренней
                                  // подтяжкой к плюсу.

  digitalWrite(PIN_LED_GREEN, LOW); // Выключает зелёный LED на старте.
  digitalWrite(PIN_LED_RED, LOW);   // Выключает красный LED на старте.
  digitalWrite(PIN_BUZZER, LOW);    // Выключает зуммер на старте.

  Wire.begin(8, 9);      // Запускает I2C: SDA на пине 8, SCL на пине 9.
  Wire.setClock(100000); // Устанавливает скорость I2C 100 кГц.

  lcd.init();      // Инициализирует LCD-дисплей.
  lcd.backlight(); // Включает подсветку LCD.

  bootAnimation(); // Показывает стартовую анимацию загрузки.

  aht.begin();     // Инициализирует датчик AHT.
  bmp.begin(0x77); // Инициализирует BMP280 по адресу 0x77.

  readSensors();         // Читает первые показания датчиков.
  lastSensor = millis(); // Запоминает время первого чтения.

  lcdOn(true);                 // Включает экран после загрузки.
  lcdUpdate();                 // Показывает первые показания на экране.
  delay(STARTUP_DISPLAY_TIME); // Держит показания на экране заданное время.

  enterStandby(); // После показа переходит в standby и гасит экран.

  digitalWrite(PIN_LED_GREEN, HIGH); // Включает зелёный LED для теста.
  delay(500); // Держит зелёный LED включённым 0.5 секунды.
  digitalWrite(PIN_LED_GREEN, LOW); // Выключает зелёный LED после теста.

  digitalWrite(PIN_LED_RED, HIGH); // Включает красный LED для теста.
  delay(500);                      // Держит красный LED включённым 0.5 секунды.
  digitalWrite(PIN_LED_RED, LOW);  // Выключает красный LED после теста.
}

// ================= LOOP =================
void loop() {                 // loop выполняется бесконечно после setup.
  bool btn = buttonPressed(); // Проверяет, была ли нажата кнопка.

  switch (mode) {       // Выбирает поведение в зависимости от текущего режима.
  case MODE_STANDBY:    // Если сейчас режим ожидания.
    handleStandby(btn); // Выполняет логику standby.
    break;              // Завершает этот case.

  case MODE_ACTIVE:    // Если сейчас активный режим.
    handleActive(btn); // Выполняет логику active.
    break;             // Завершает этот case.

  case MODE_ALARM:    // Если сейчас режим тревоги.
    handleAlarm(btn); // Выполняет логику alarm.
    break;            // Завершает этот case.
  }
}