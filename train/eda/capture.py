import argparse
import os
import sys
from datetime import datetime
import serial

DEV = '/dev/ttyACM0'
BAUD = 115200


def capture():
   """
    Capture raw data from serial input
   """
    os.makedirs(f'data/raw/{args.gesture}', exist_ok=True)
    data = ''
    filename = f'data/raw/{args.gesture}/{args.gesture}_{args.person}_{datetime.now().isoformat()}.txt'
    try:
        with serial.Serial(DEV, BAUD) as ser:
            print('Capture...')
            while (True):
                try:
                    new_line = str(ser.readline(), 'utf-8')
                    data += new_line
                except (serial.SerialException) as error:
                    print(error, flush=True)
                    data = ''
    except KeyboardInterrupt:
        print('Saving')
        with open(filename, 'w+', encoding='utf-8') as f:
            f.write(data)
        sys.exit()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("--gesture", "-g")
    parser.add_argument("--person", "-p")
    args = parser.parse_args()
    capture()
