#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <customs/encoderPORTD.hpp>
#include <customs/buzzer.hpp>
#include <customs/tm1637.hpp>

// Пины
#define S1 5
#define S2 6
#define KEY 7
#define BUZZ 8
#define DIO 9
#define CLK 10
// Для настроек
#define DISPLAY_BRIGHTNESS 1
#define MAX_ALARM_TICKS 30
#define ROTATE_SOUND_DURATION 30
#define ROTATE_SOUND_DURATION_SHORT 15
#define BLINKING_SOUND_DURATION 60      // ROTATE_SOUND_DURATION * 2
#define SECONDS_TO_DELAY_POSITION 0     // 0 - 3


// Режимы
enum State
{
  START,
  SET_START_DELAY,
  SET_MIN,
  SET_SEC,
  DELAY_BEFORE,
  GOING_DOWN,
  ALARM,
  DROP
};

struct Time
{
  enum class Field : uint8_t
  {
    Sec,
    Min,
    Packed
  };

  uint16_t get(Field field)
  {
    switch (field)
    {
    case Field::Sec:
      return _sec;
    case Field::Min:
      return _min;
    case Field::Packed:
      return (_min * 100) + _sec;
    }
  }

  void increment(Field field)
  {
    switch (field)
    {
    case Field::Sec:
      _sec++;
      _checkTime();
      break;
    case Field::Min:
      _min++;
      _checkTime();
      break;
    default:
      break;
    }
  }
  void decrement(Field field)
  {
    switch (field)
    {
    case Field::Sec:
      _sec--;
      _checkTime();
      break;
    case Field::Min:
      _min--;
      _checkTime();
      break;
    default:
      break;
    }
  }

  void reSet(uint8_t min, uint8_t sec)
  {
    _min = min;
    _sec = sec;
  }



private:
  int8_t _min = 0, _sec = 0;

  void _checkTime()
  {
    if (_sec == 60)
    {
      _sec = 0;
      _min++;
    }
    else if (_sec < 0)
    {
      _sec = 59;
      _min--;
    }
    if (_min > 99)
    {
      _min = 0;
      _sec = 0;
    }
    else if (_min < 0)
    {
      _min = 99;
      _sec = 59;
    }
  }
};

EncoderPORTD encoder(S1, S2, KEY);
Buzzer buzzer(BUZZ);
TM1637 display(DIO, CLK);

volatile State state = START;
Time time;
volatile bool halfSecondFlag = false, timeGoneFlag = false;
bool pointState = false, alarmState = false, teaMode = false;
uint8_t ticks = 0, alarmTicks = 0, secondsToDelayToStart = 3;
uint8_t lastSecondsSet = 0,
        lastMinutesSet = 0,
        lastSecondsToDelaySet = secondsToDelayToStart;

// MAIN
void init();
void go(EncoderPORTD::State);
void update(EncoderPORTD::State);
// support
void startTimer();
void stopTimer();

// ####################################################################
//                                                                    |
ISR(PCINT2_vect)
{
  encoder.interruptRoutine();
}

ISR(TIMER0_COMPA_vect)
{
  buzzer.interruptRoutine();
}

ISR(TIMER1_COMPA_vect)
{
  halfSecondFlag = true;
}

int main()
{
  init();

  while (true)
  {
    EncoderPORTD::State encoderState = encoder.readState();
    go(encoderState);
    update(encoderState);
  }
  return 0;
}
//                                                                    |
// ####################################################################

void init()
{
  // Предарительно настроить таймер на СТС режим и включить прерывание
  TCCR1A = 0;
  TCCR1B = 0;
  TCCR1C = 0;
  TCCR1B |= (1 << WGM12);
  TIMSK1 |= (1 << OCIE1A);
  sei();
}

void go(const EncoderPORTD::State encoderState)
{
  switch (state)
  {
  case START:
    // Установить яркость дисплея, убрать тире, запустить анимацию, сделать паузу
    // Издать звук, отобразть подчеркивание на всех сегментах
    display.setBrightness(DISPLAY_BRIGHTNESS);
    display.showChar(0);
    // Главная анимация
    display.hello();
    // Анимация для установки задержки пуска
    buzzer.playNote(Buzzer::Notes::G, ROTATE_SOUND_DURATION * 2);
    _delay_ms(200);
    display.showChar(3);
    buzzer.playNote(Buzzer::Notes::G, ROTATE_SOUND_DURATION * 2);
    _delay_ms(200);
    display.showChar(2);
    buzzer.playNote(Buzzer::Notes::G, ROTATE_SOUND_DURATION * 2);
    _delay_ms(200);
    if (teaMode)
      display.show((uint8_t)secondsToDelayToStart, SECONDS_TO_DELAY_POSITION);
    break;
  case SET_START_DELAY:
    // Если обнаружен поворот энкодера то
    // Уменьшить/увеличить секунды, издать звук, обновить дисплей
    if (encoderState == EncoderPORTD::State::rotateRight)
    {
      secondsToDelayToStart > 9 ? secondsToDelayToStart = 9 : secondsToDelayToStart++;
      display.show((uint8_t)secondsToDelayToStart, SECONDS_TO_DELAY_POSITION);
      buzzer.playNote(Buzzer::Notes::G, ROTATE_SOUND_DURATION_SHORT);
    }
    else if (encoderState == EncoderPORTD::State::rotateLeft)
    {
      secondsToDelayToStart < 0 ? secondsToDelayToStart = 0 : secondsToDelayToStart--;
      display.show((uint8_t)secondsToDelayToStart, SECONDS_TO_DELAY_POSITION);
      buzzer.playNote(Buzzer::Notes::G, ROTATE_SOUND_DURATION_SHORT);
    }
    break;
  case SET_MIN:
    // Если обнаружен поворот энкодера то
    // Уменьшить/увеличить минуты, издать звук и обновить дисплей
    if (encoderState == EncoderPORTD::State::rotateRight)
    {
      time.increment(Time::Field::Min);
      display.showTime(TM1637::Razr::MIN, time.get(Time::Field::Min));
      buzzer.playNote(Buzzer::Notes::G, ROTATE_SOUND_DURATION);
    }
    if (encoderState == EncoderPORTD::State::rotateLeft)
    {
      time.decrement(Time::Field::Min);
      display.showTime(TM1637::Razr::MIN, time.get(Time::Field::Min));
      buzzer.playNote(Buzzer::Notes::D, ROTATE_SOUND_DURATION);
    }
    break;
  case SET_SEC:
    // Если обнаружен поворот энкодера то
    // Уменьшить/увеличить секунды, издать звук и обновить дисплей
    if (encoderState == EncoderPORTD::State::rotateRight)
    {
      time.increment(Time::Field::Sec);
      buzzer.playNote(Buzzer::Notes::G, ROTATE_SOUND_DURATION);
      display.show(time.get(Time::Field::Packed));
    }
    if (encoderState == EncoderPORTD::State::rotateLeft)
    {
      time.decrement(Time::Field::Sec);
      buzzer.playNote(Buzzer::Notes::D, ROTATE_SOUND_DURATION);
      display.show(time.get(Time::Field::Packed));
    }
    break;
  case DELAY_BEFORE:
  // Каждую секунду издавать звук и делать декремент секунд задержки запуска
  // Если закончилось время то поднять флаг timeGoneFlag
  if (halfSecondFlag)
  {
    halfSecondFlag = false;
    ticks++;
    if (ticks > 1)
    {
      ticks = 0;
      secondsToDelayToStart--;
      display.show((uint8_t)secondsToDelayToStart, SECONDS_TO_DELAY_POSITION);
      buzzer.playNote(Buzzer::Notes::G, BLINKING_SOUND_DURATION);
    }
    if (secondsToDelayToStart <= 0)
      timeGoneFlag = true;
  }
  break;
  case GOING_DOWN:
    // Каждые пол секунды переключать двоеточие на дисплее
    // Каждую секунду делать декремент секунд
    // Если закончилось время то поднять флаг timeGoneFlag
    if (halfSecondFlag)
    {
      display.setPoint(pointState ^= 1);
      halfSecondFlag = false;
      ticks++;
      if (ticks > 1)
      {
        ticks = 0;
        time.decrement(Time::Field::Sec);
        display.show(time.get(Time::Field::Packed));
      }
      if (time.get(Time::Field::Min) == 0 && time.get(Time::Field::Sec) == 0)
        timeGoneFlag = true;
    }
    break;
  case ALARM:
    // Если поднят полсекундный флаг то сбросить его и переключить пищалку - ON/OFF
    if (halfSecondFlag)
    {
      halfSecondFlag = false;
      alarmState ^= 1;
      if (alarmState)
      {
        buzzer.playNote(Buzzer::Notes::B);
        display.setBrightness(DISPLAY_BRIGHTNESS);
        alarmTicks++; // Проверка в update(), сброс в case DROP
      }
      else
      {
        buzzer.off();
        display.setBrightness(0);
      }
    }
    break;
  case DROP:
    // Выключить аппаратный таймер, выключить сигнал, сбросить время и флаги
    stopTimer();
    buzzer.off();
    switch (teaMode)
    {
      case true:
      time.reSet(lastMinutesSet, lastSecondsSet);
      break;
      case false:
      time.reSet(0, 0);
      break;
    }
    secondsToDelayToStart = lastSecondsToDelaySet;
    timeGoneFlag = false;
    halfSecondFlag = false;
    alarmState = false;
    alarmTicks = 0;
    pointState = false;
    display.setPoint(pointState);
    break;
  }
}

void update(const EncoderPORTD::State encoderState)
{
  switch (state)
  {
  case START:
    // Безусловный переход в SET_START_DELAY
    state = SET_START_DELAY;
    break;
  case SET_START_DELAY:
    // Если нажата кнопка то поморгать дисплеем и попищать пищалкой
    // Перейти в SET_MIN
    if (encoderState == EncoderPORTD::State::press)
    {
      secondsToDelayToStart > 0 ? teaMode = true : teaMode = false;
      lastSecondsToDelaySet = secondsToDelayToStart;
      display.show((uint8_t)secondsToDelayToStart, SECONDS_TO_DELAY_POSITION);
      display.off();
      _delay_ms(BLINKING_SOUND_DURATION * 5);
      for (uint8_t i = 0; i < secondsToDelayToStart; i++)
      {
        buzzer.playNote(Buzzer::Notes::G, BLINKING_SOUND_DURATION);
        display.on();
        _delay_ms(BLINKING_SOUND_DURATION * 2);
        display.off();
        _delay_ms(BLINKING_SOUND_DURATION * 2);
      }
      display.setBrightness(1);
      pointState = true;
      display.setPoint(pointState);
      switch (teaMode)
      {
        case true:
        display.show(time.get(Time::Field::Packed));
        break;
        case false:
        display.showChar(1);
        break;
      }
      state = SET_MIN;
    }
  break;
  case SET_MIN:
    // Если нажата кнопка то перейти в SET_SEC
    if (encoderState == EncoderPORTD::State::press)
    {
      display.showTime(TM1637::Razr::MIN, time.get(Time::Field::Min));
      lastMinutesSet = time.get(Time::Field::Min);
      state = SET_SEC;
    }
    break;
  case SET_SEC:
    // Если нажата кнопка то
    // Запустить timer1, проверить задержку запуска, если > 0
    // ТО подготовить экран, издать звук, перейти в GOING_DOWN
    // ИНАЧЕ перейти в DELAY_BEFORE
    if (encoderState == EncoderPORTD::State::press)
    {
      lastSecondsSet = time.get(Time::Field::Sec);
      if (secondsToDelayToStart > 0 && time.get(Time::Field::Packed) > 0)
      {
        pointState = false;
        display.setPoint(pointState);
        display.showChar(2);
        display.show((uint8_t)secondsToDelayToStart, SECONDS_TO_DELAY_POSITION);
        buzzer.playNote(Buzzer::Notes::G, BLINKING_SOUND_DURATION);
        state = DELAY_BEFORE;
      }
      else
      {
        display.show(time.get(Time::Field::Packed));
        if (time.get(Time::Field::Packed) == 0)
          state = ALARM;
        else
          state = GOING_DOWN;
      }
      startTimer();
    }
    break;
  case DELAY_BEFORE:
  // Если счетчик дошел до нуля (timeGoneFlag == true) то ПЕРЕЙТИ в GOING_DOWN
  if (timeGoneFlag)
  {
    state = GOING_DOWN;
    pointState = true;
    display.setPoint(pointState);
    display.show(time.get(Time::Field::Packed));
    buzzer.playNote(Buzzer::Notes::B, 300);
    ticks = 0;
    timeGoneFlag = false;
  }
  break;
  case GOING_DOWN:
    // Если счетчик дошел до нуля (timeGoneFlag == true) то ПЕРЕЙТИ в ALARM
    if (timeGoneFlag == true)
    {
      state = ALARM;
      display.showChar(1);
    }
    break;
  case ALARM:
    // Если кнопка нажата или закончилось время сигнализирования перейти в DROP
    if (encoderState == EncoderPORTD::State::press || alarmTicks > MAX_ALARM_TICKS)
      state = DROP;
    break;
  case DROP:
    // Безусловный переход в START
    state = START;
    break;
  }
}

void startTimer()
{
  OCR1A = 31249;
  TCCR1B |= (1 << CS12);
}

void stopTimer()
{
  TCCR1B &= ~(1 << CS12);
  TIFR1 = 0;
  TCNT1 = 0;
}
