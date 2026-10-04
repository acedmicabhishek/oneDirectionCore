#include "serial_controller_windows.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static HANDLE hSerial = INVALID_HANDLE_VALUE;
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
static int fd = -1;
#endif

int OD_Hardware_Init(const char* com_port, int baud_rate) {
#ifdef _WIN32
    if (!com_port || !com_port[0]) return 0;
    OD_Hardware_Close();

    /* COM10 and above only open through the \\.\ device namespace. */
    char path[64];
    if (strncmp(com_port, "\\\\.\\", 4) == 0) snprintf(path, sizeof(path), "%s", com_port);
    else snprintf(path, sizeof(path), "\\\\.\\%s", com_port);

    hSerial = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hSerial == INVALID_HANDLE_VALUE) return 0;

    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    if (!GetCommState(hSerial, &dcbSerialParams)) { OD_Hardware_Close(); return 0; }
    dcbSerialParams.BaudRate = baud_rate;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;
    if (!SetCommState(hSerial, &dcbSerialParams)) { OD_Hardware_Close(); return 0; }

    /* Bound writes so a stalled device cannot hang the caller. */
    COMMTIMEOUTS timeouts = {0};
    timeouts.WriteTotalTimeoutConstant = 50;
    SetCommTimeouts(hSerial, &timeouts);
    return 1;
#else
    fd = open(com_port, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) return 0;

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) return 0;

    speed_t speed = B9600;
    if (baud_rate == 115200) speed = B115200;

    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_iflag &= ~IGNBRK;
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 5;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) return 0;
    return 1;
#endif
}

int OD_Hardware_SendDirectionLog(float azimuth) {
    
    int sector = (int)((azimuth + 22.5f) / 45.0f) % 8;
    if (sector < 0) sector += 8;

    unsigned char payload = (1 << sector);

#ifdef _WIN32
    if (hSerial == INVALID_HANDLE_VALUE) return 0;
    DWORD bytesWritten = 0;
    if (!WriteFile(hSerial, &payload, 1, &bytesWritten, NULL)) return 0;
    return bytesWritten == 1;
#else
    if (fd < 0) return 0;
    return write(fd, &payload, 1) == 1;
#endif
}

void OD_Hardware_Close(void) {
#ifdef _WIN32
    if (hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;
    }
#else
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
#endif
}
