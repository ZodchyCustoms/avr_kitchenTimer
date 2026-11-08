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
#define ROTATE_SOUND_DURATION 30
// Для настроек
#define DISPLAY_BRIGHTNESS 1
#define MAX_ALARM_TICKS 30

// Режимы
enum State
{
  START,
  SET_MIN,
  SET_SEC,
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

  void reset()
  {
    _min = 0;
    _sec = 0;
  }

private:
  int8_t _min = 0, _sec = 0;

void _checkTime()
  {
    if (_sec == 60)
    {
      _sec = 0;
      _min++;
    } else if (_sec < 0)
    {
      _sec = 59;
      _min--;
    }
    if (_min > 99)
    {
      _min = 0;
      _sec = 0;
    } else if (_min < 0)
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
bool pointState = false, alarmState = false;
uint8_t ticks = 0, alarmTicks = 0;


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
    // Установить яркость дисплея, убрать тире, запустить анимацию, включить двоеточие
    display.setBrightness(DISPLAY_BRIGHTNESS);
    display.showChar(0);
    display.hello();
    pointState = true;
    display.setPoint(pointState);
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
      // display.showTime(TM1637::Razr::SEC, time.getSec());
    }
    if (encoderState == EncoderPORTD::State::rotateLeft)
    {
      time.decrement(Time::Field::Sec);
      buzzer.playNote(Buzzer::Notes::D, ROTATE_SOUND_DURATION);
      display.show(time.get(Time::Field::Packed));
      // display.showTime(TM1637::Razr::SEC, time.getSec());
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
      if (alarmState){
        buzzer.playNote(Buzzer::Notes::B);
        display.setBrightness(DISPLAY_BRIGHTNESS);
        alarmTicks++;   // Проверка в update(), сброс в case DROP
      } else
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
    time.reset();
    timeGoneFlag = false;
    halfSecondFlag = false;
    alarmState = false;
    alarmTicks = 0;
    break;
  }
}

void update(const EncoderPORTD::State encoderState)
{
  switch (state)
  {
    case START:
    // Безусловный переход в SET_MIN
    state = SET_MIN;
    break;
    case SET_MIN:
    // Если нажата кнопка то перейти в SET_SEC
    if (encoderState == EncoderPORTD::State::press)
      state = SET_SEC;
    break;
    case SET_SEC:
    // Если нажата кнопка то
    // ЗАПУСТИТЬ timer1, ПЕРЕЙТИ в GOING_DOWN
    if (encoderState == EncoderPORTD::State::press)
    {
      state = GOING_DOWN;
      startTimer();
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

