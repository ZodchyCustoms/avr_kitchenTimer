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
#define ROTATE_NOTE_DURATION 40

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

// Заменить инкремент/декремент на одну функцию с параметрами?
struct Time
{
  uint8_t getMin()
  {
    return (uint8_t) _min;
  }

  uint8_t getSec()
  {
    return (uint8_t) _sec;
  }

  void incrementSec()
  {
    _sec++;
    _checkTime();
  }
  
  void decrementSec()
  {
    _sec--;
    _checkTime();
  }

  void incrementMin()
  {
    _min++;
    _checkTime();
  }
  
  void decrementMin()
  {
    _min--;
    _checkTime();
  }

  void reset()
  {
    _sec = 0;
    _min = 0;
  }
  
private:
  int8_t _sec = 0;
  int8_t _min = 0;
  
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
uint8_t ticks = 0;


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
    // Отобразить на дисплее приглашение к вводу
    display.showChar(0);
    display.hello();
    pointState = true;
    display.setPoint(pointState);
    break;
    case SET_MIN:
    // Опросить энкодер и если обнаружен поворот то
    // Уменьшить/увеличить минуты, издать звук и обновить дисплей
    if (encoderState == EncoderPORTD::State::rotateRight)
    {
      time.incrementMin();
      display.showTime(TM1637::Razr::MIN, time.getMin());
      buzzer.playNote(Buzzer::Notes::G, ROTATE_NOTE_DURATION);
    }
    if (encoderState == EncoderPORTD::State::rotateLeft)
    {
      time.decrementMin();
      display.showTime(TM1637::Razr::MIN, time.getMin());
      buzzer.playNote(Buzzer::Notes::D, ROTATE_NOTE_DURATION);
    }
    break;
    case SET_SEC:
    // Опросить энкодер и если обнаружен поворот то
    // Уменьшить/увеличить секунды, издать звук и обновить дисплей
    if (encoderState == EncoderPORTD::State::rotateRight)
    {
      time.incrementSec();
      buzzer.playNote(Buzzer::Notes::G, ROTATE_NOTE_DURATION);
      display.showTime(TM1637::Razr::SEC, time.getSec());
    }
    if (encoderState == EncoderPORTD::State::rotateLeft)
    {
      time.decrementSec();
      buzzer.playNote(Buzzer::Notes::D, ROTATE_NOTE_DURATION);
      display.showTime(TM1637::Razr::SEC, time.getSec());
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
        time.decrementSec();
        uint16_t vremes = time.getSec() + (time.getMin() * 100);
        display.show(vremes);
      }
      if (time.getMin() == 0 && time.getSec() == 0)
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
        buzzer.playNote(Buzzer::Notes::B);
      else
        buzzer.off();
    }
    break;
    case DROP:
    // Выключить аппаратный таймер, выключить сигнал, сбросить время и флаги
    stopTimer();
    buzzer.off();
    time.reset();
    timeGoneFlag = false;
    halfSecondFlag = false;
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
      startTimer();
      state = GOING_DOWN;
    }
    break;
    case GOING_DOWN:
    // Если счетчик дошел до нуля (timeGoneFlag == true) то
    // ПЕРЕНАСТРОИТЬ timer1 на сигнал тревоги - прерывание раз в пол секунды, ПЕРЕЙТИ в ALARM
    if (timeGoneFlag == true)
    {
      state = ALARM;
    }
    break;
    case ALARM:
    // Если кнопка нажата перейти в DROP
    if (encoderState == EncoderPORTD::State::press)
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

