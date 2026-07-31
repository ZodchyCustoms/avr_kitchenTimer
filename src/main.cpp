/*
Timer. Using TIMER_1
*/

#include <avr/io.h>
#include <avr/interrupt.h>
#include <customize/pins.h>
#include <customize/TM1637.h>
#include <customize/buzzer.h>

void setup();

class EncoderPORTD
{
public:
  EncoderPORTD(uint8_t s1, uint8_t s2, uint8_t key)
  {
    this->_s1 = s1;
    this->_s2 = s2;
    this->_key = key;
    setPinInput(_s1);
    setPinInput(_s2);
    setPinInput(_key);
    PCICR |= (1 << PCIE2);
    PCMSK2 |= (1 << _s1) | (1 << _s2) | (1 << _key);
  }
 
  // Return a code of current state of encoder
  uint8_t read()
  {
    _code <<= 2;
    _code |= (pinRead(_s1) << 0) | (pinRead(_s2) << 1);   // right = 180, left = 120    <--- need to test with any pins
    return _code;
  }

private:
  uint8_t _s1, _s2, _key, _code;

};


class Timer {
public:
  Timer(uint8_t s1, uint8_t s2, uint8_t key, uint8_t dioPin, uint8_t clkPin, uint8_t buzz) : _encoder(s1, s2, key), _display(dioPin, clkPin), _tone(buzz)
  {
    _keyPin = key;
    TCCR1B |= (1 << CS12);
    TIMSK1 |= (1 << OCIE1A) | (1 << OCIE1B);
    OCR1A = 31250;    // 0.5 sec - points blink
    OCR1B = 62500;    // 1 sec   - decrement seconds
    
    _display.showChar(1);
    _delay_ms(500);
    _display.point(true);
    _delay_ms(500);
  }

  void voiceOver()
  {
    _tone.meandrTick();
  }

  void updateHalfSecond()
  {
    switch (_mode)
    {
      case MODE_GOING:
      _display.point( (_pointState ^= 1) );
      break;
      case MODE_FINISH:
      _tone.off();
      break;
      default:
      break;
    }
  }

  void updateSecond()
  {
    switch (_mode)
    {
      case MODE_GOING:
      _goingDown();
      break;
      case MODE_FINISH:
      _tone.playNote(5, 500);
      break;
      default:
      break;
    }
  }

    void encode()
  {
    if (pinRead(_keyPin))
      _modeSwitch();
    switch (_mode)
    {
      case MODE_SETUP_MIN:
      _settingUpTime(_encoder.read(), _display.RAZR_MIN);
      _display.showTime(_min, _display.RAZR_MIN);
      break;
      case MODE_SETUP_SEC:
      _settingUpTime(_encoder.read(), _display.RAZR_SEC);
      _display.showTime(_sec, _display.RAZR_SEC);
      default:
      break;
    }
  }


private:  
  enum  // Режимы
  {
    MODE_SETUP_MIN = 0,
    MODE_SETUP_SEC = 1,
    MODE_GOING = 2,
    MODE_FINISH = 3
  };

  enum  // Поворот энкодера
  {
    CODE_RIGHT = 180,
    CODE_LEFT = 120
  };

  EncoderPORTD _encoder;
  TM1637 _display;
  Buzzer _tone;
  volatile bool _pointState = false;
  volatile uint8_t _keyPin, _mode = MODE_SETUP_MIN, _sec = 0, _min = 0;


  void _modeSwitch()
  {
    //_mode == MODE_FINISH ? _mode = MODE_SETUP_MIN : _mode++;
    _mode++;
    switch (_mode)
    {
      case MODE_SETUP_MIN:
      _tone.playNote(3, 100);
      break;
      case MODE_SETUP_SEC:
      _tone.playNote(3, 100);
      break;
      case MODE_GOING:
      _tone.playNote(3, 300);
      break;
      case MODE_FINISH:
      _tone.playNote(0, 200);
      _mode = MODE_SETUP_MIN;
      break;
      default:
      break;
    }
  }

  void _settingUpTime(uint8_t direction, uint8_t razrSelected)
  {
    switch (direction)
    {
      case CODE_LEFT:
      _decrement(razrSelected);
      _tone.playNote(0, 20);
      break;
      case CODE_RIGHT:
      _increment(razrSelected);
      _tone.playNote(5, 20);
      break;
    }
    if (_sec == 60)
      _sec = 0;
    if (_sec > 60)
      _sec = 59;
    if (_min == 100)
      _min = 0;
    if (_min == 255)
      _min = 99;
  }

  void _decrement(uint8_t razr)
  {
    switch (razr)
    {
      case _display.RAZR_MIN:
      _min--;
      break;
      case _display.RAZR_SEC:
      _sec--;
      break;
      default:
      break;
    }
  }

  void _increment(uint8_t razr)
  {
    switch (razr)
    {
      case _display.RAZR_MIN:
      _min++;
      break;
      case _display.RAZR_SEC:
      _sec++;
      break;
    }
  }

  void _goingDown()
  {
    if (_sec == 255)
    {
      if (_min != 0)
      {
        _min--;
        _sec = 59;
      } else
      {
        _display.point(true);
        _display.showChar(1);
        _mode = MODE_FINISH;
      }
    }
    _display.show( (_min * 100) + _sec );
    _sec--;
  }

  
  
  void _pointBlink()
  {
    _display.point(_pointState ^= 1);
  }

  void _pointOff()
  {
    if (_pointState)
      {
        _pointState = false;
        _display.point(_pointState);
      }
  }



};

Timer timer(5, 6, 7, 8, 9, 10);

ISR(TIMER0_COMPA_vect)
{
  timer.voiceOver();
}

ISR(TIMER1_COMPA_vect)
{
  timer.updateHalfSecond();
}

ISR(TIMER1_COMPB_vect)
{
  timer.updateSecond();
  TCNT1H = 0;
  TCNT1L = 0;
}

ISR(PCINT2_vect)
{
  timer.encode();
}


int main()
{
  setup();


  while (true)
  {

  }
  return 0;
}


void setup()
{
  DDRD = 0;
  PORTD = 255;
  DDRB |= (1 << PB5);
  


  sei();
}



