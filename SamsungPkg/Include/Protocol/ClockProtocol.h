/** @file
  Clock protocol for MSM8996 GCC (Global Clock Controller).

  Provides a simple interface for enabling and configuring GCC clocks
  from outside the GccClockDxe driver.

  Copyright (c) 2024, Samsung / TianoCore Contributors.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef CLOCK_PROTOCOL_H_
#define CLOCK_PROTOCOL_H_

#define CLOCK_PROTOCOL_GUID \
  { 0xa1b2c3d4, 0xe5f6, 0x7890, { 0xa1, 0xb2, 0xc3, 0xd4, 0xe5, 0xf6, 0x78, 0x90 } }

typedef struct _CLOCK_PROTOCOL CLOCK_PROTOCOL;

/**
  Enable a peripheral clock by its GCC CBCR register offset.

  @param[in]  This     Pointer to CLOCK_PROTOCOL.
  @param[in]  CbcrOffset  CBCR register offset from GCC base.

  @retval EFI_SUCCESS       Clock enabled.
  @retval EFI_TIMEOUT       Clock gated after timeout.
  @retval EFI_DEVICE_ERROR  Hardware error.
**/
typedef
EFI_STATUS
(EFIAPI *CLOCK_ENABLE)(
  IN CLOCK_PROTOCOL  *This,
  IN UINT32           CbcrOffset
  );

/**
  Disable a peripheral clock by its GCC CBCR register offset.

  @param[in]  This     Pointer to CLOCK_PROTOCOL.
  @param[in]  CbcrOffset  CBCR register offset from GCC base.

  @retval EFI_SUCCESS  Clock disabled.
**/
typedef
EFI_STATUS
(EFIAPI *CLOCK_DISABLE)(
  IN CLOCK_PROTOCOL  *This,
  IN UINT32           CbcrOffset
  );

/**
  Set the source and divider for an RCG (Root Clock Generator).

  @param[in]  This        Pointer to CLOCK_PROTOCOL.
  @param[in]  CmdRcgr     CMD_RCGR register offset from GCC base.
  @param[in]  CfgRcgr     CFG_RCGR register offset from GCC base.
  @param[in]  Source      Source select bits for CFG_RCGR[10:8].
  @param[in]  Divisor     Half-integer divisor (0 = /1, 1 = /2, ...).
  @param[in]  M           MND M value (0 = bypass MND).
  @param[in]  N           MND N value.
  @param[in]  D           MND D value.

  @retval EFI_SUCCESS  RCG updated.
  @retval EFI_TIMEOUT  Update bit did not self-clear.
**/
typedef
EFI_STATUS
(EFIAPI *CLOCK_SET_RATE)(
  IN CLOCK_PROTOCOL  *This,
  IN UINT32           CmdRcgr,
  IN UINT32           CfgRcgr,
  IN UINT32           Source,
  IN UINT32           Divisor,
  IN UINT32           M,
  IN UINT32           N,
  IN UINT32           D
  );

struct _CLOCK_PROTOCOL {
  CLOCK_ENABLE    Enable;
  CLOCK_DISABLE   Disable;
  CLOCK_SET_RATE  SetRate;
};

extern EFI_GUID  gClockProtocolGuid;

#endif // CLOCK_PROTOCOL_H_
