/** @file
  Serial port library for Qualcomm MSM / UARTDM v1.4 (lsuart-v14).

  Target UART: serial@075b0000 on MSM8996 (Samsung GTS3L LTE).
  The bootloader (XBL / ABL / LK) initialises the UART before handing
  off to UEFI, so we only need to poll-and-write; no baud-rate setup is
  done here.

  Register layout follows the MSM UARTDM v1.4 non-DM (lite) path:
    SR   @ base+0x008  (read)  — status register
    TF   @ base+0x100  (write) — TX FIFO
    RF   @ base+0x140  (read)  — RX FIFO

  Copyright (c) 2024, Samsung / TianoCore Contributors.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Base.h>
#include <Library/SerialPortLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>

#include <Platform/GTS3LLte.h>

// Convenience macro — read/write 32-bit MMIO relative to UART base
// PcdMsmUartBase is UINT32 (MSM8996 peripheral address space < 4 GB)
#define UART_BASE             ((UINTN)FixedPcdGet32 (PcdMsmUartBase))
#define UART_READ(reg)        MmioRead32  (UART_BASE + (reg))
#define UART_WRITE(reg, val)  MmioWrite32 (UART_BASE + (reg), (val))

/**
  Initialise the serial port.

  The bootloader already configured baud rate and GPIO pinmux, so we
  only reset the error-status bits via the command register.

  @retval RETURN_SUCCESS   Always.
**/
RETURN_STATUS
EFIAPI
SerialPortInitialize (
  VOID
  )
{
  // Reset error status (CR command = 0x10 → reset error status)
  UART_WRITE (UART_CR, 0x10);
  // Enable UART transmitter and receiver (CR = 0x05 : TX_EN | RX_EN)
  UART_WRITE (UART_CR, 0x05);
  return RETURN_SUCCESS;
}

/**
  Write data to the serial port.

  @param  Buffer           Pointer to data buffer.
  @param  NumberOfBytes    Number of bytes to write.
  @return                  Number of bytes written.
**/
UINTN
EFIAPI
SerialPortWrite (
  IN UINT8  *Buffer,
  IN UINTN  NumberOfBytes
  )
{
  UINTN  BytesSent;
  UINT8  Byte;

  for (BytesSent = 0; BytesSent < NumberOfBytes; BytesSent++) {
    Byte = Buffer[BytesSent];

    // Translate '\n' to '\r\n' for serial terminals
    if (Byte == '\n') {
      // Wait for TX FIFO to have space
      while ((UART_READ (UART_SR) & UART_SR_TXRDY) == 0) {
      }
      UART_WRITE (UART_TF, '\r');
    }

    // Wait for TX FIFO to have space
    while ((UART_READ (UART_SR) & UART_SR_TXRDY) == 0) {
    }
    UART_WRITE (UART_TF, Byte);
  }

  return BytesSent;
}

/**
  Read data from the serial port (non-blocking).

  @param  Buffer           Pointer to destination buffer.
  @param  NumberOfBytes    Maximum number of bytes to read.
  @return                  Number of bytes actually read.
**/
UINTN
EFIAPI
SerialPortRead (
  OUT UINT8  *Buffer,
  IN  UINTN  NumberOfBytes
  )
{
  UINTN  BytesRead;

  for (BytesRead = 0; BytesRead < NumberOfBytes; BytesRead++) {
    // If RX FIFO is empty, stop
    if ((UART_READ (UART_SR) & UART_SR_RXRDY) == 0) {
      break;
    }
    Buffer[BytesRead] = (UINT8)(UART_READ (UART_RF) & 0xFF);
  }

  return BytesRead;
}

/**
  Poll for received data.

  @retval TRUE   A byte is available in the RX FIFO.
  @retval FALSE  No data available.
**/
BOOLEAN
EFIAPI
SerialPortPoll (
  VOID
  )
{
  return ((UART_READ (UART_SR) & UART_SR_RXRDY) != 0);
}

/**
  Set serial port attributes.  Not supported on this platform.

  @retval RETURN_UNSUPPORTED  Always.
**/
RETURN_STATUS
EFIAPI
SerialPortSetAttributes (
  IN OUT UINT64              *BaudRate,
  IN OUT UINT32              *ReceiveFifoDepth,
  IN OUT UINT32              *Timeout,
  IN OUT EFI_PARITY_TYPE     *Parity,
  IN OUT UINT8               *DataBits,
  IN OUT EFI_STOP_BITS_TYPE  *StopBits
  )
{
  return RETURN_UNSUPPORTED;
}

/**
  Set serial port control bits.  Not supported on this platform.

  @retval RETURN_UNSUPPORTED  Always.
**/
RETURN_STATUS
EFIAPI
SerialPortSetControl (
  IN UINT32  Control
  )
{
  return RETURN_UNSUPPORTED;
}

/**
  Get serial port control bits.

  @param  Control   Returns the current control state.
  @retval RETURN_SUCCESS  Always.
**/
RETURN_STATUS
EFIAPI
SerialPortGetControl (
  OUT UINT32  *Control
  )
{
  *Control = 0;
  return RETURN_SUCCESS;
}
