import matplotlib.animation as animation
import matplotlib.pyplot as plt
import serial

DEV = '/dev/ttyACM0'
BAUD = 115200
y_labels = {0: 'Quat', 1: 'Acc', 2: 'Mag', 3: 'Gyr'}
colors = ['g', 'b', 'r', 'c']


# This function is called periodically from FuncAnimation
def animate(*args):
    try:
        # Aquire and parse data from serial port
        line = ser.readline()  # ascii
        line_as_list = line.split(b',')
        line_as_list = [float(item) for item in line_as_list]
        last_x = data[0][-1] if any(data) else 0
        data[0].append(last_x + 1)
        for i in range(len(line_as_list)):
            data[i + 1].append(line_as_list[i])
            ax_num = max((i - 1) // 3, 0)
            ax[ax_num].plot(data[0], data[i + 1], c=colors[i % 4])
            ax[ax_num].set_ylabel(y_labels[ax_num])
    except Exception as e:
        print(e)


if __name__ == '__main__':
    # initialize serial port
    ser = serial.Serial()
    ser.port = DEV
    ser.baudrate = BAUD
    ser.timeout = 10
    ser.open()

    if ser.is_open == True:
        print("\nAll right, serial port now open. Configuration:\n")
        print(ser, "\n")  # print serial parameters

    # Create figure for plotting
    fig, ax = plt.subplots(4)
    data = [[] for i in range(14)]
    ani = animation.FuncAnimation(fig, animate, interval=250)
    plt.show()
