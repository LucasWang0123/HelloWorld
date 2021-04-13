#include <Uefi.h>
#include <Library/PcdLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Protocol/UsbIo.h>
#include <Protocol/PciIo.h>
#include <Protocol/PciRootBridgeIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/DevicePathToText.h>
#include <Protocol/IdeControllerInit.h>
#include <Protocol/SimpleTextOut.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/Lucas.h>
#include <Protocol/Cpu.h>
#include <Protocol/Usb2HostController.h>
#include <Library/DebugLib.h>
#include <Library/IoAccessLib.h>
#include <Library/DevicePathLib.h>11111
#include <Hello2.efi.h>

#define CONFIG_FCH_MMIO_BASE                     0xFED80000
#define EFI_GLOBAL_VARIABLE \
  { 0x8BE4DF61, 0x93CA, 0x11d2, {0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C} }

#define SCT_BDS_SERVICES_PROTOCOL_GUID \
  { 0xb7646a4, 0x6b44, 0x4332, 0x85, 0x88, 0xc8, 0x99, 0x81, 0x17, 0xf2, 0xef }

typedef CHAR8 * PCHAR8;                 // pointer to an 8-bit character.
typedef CHAR16 * PCHAR16;                // pointer to a 16-bit character.
typedef UINT8 * PUINT8;                 // pointer to an unsigned 8-bit value.

typedef struct _LOAD_OPTION_OBJECT {

  UINT16 OptionNumber;

  #define SCT_BM_INVALID_OPTION_NUMBER ((1 << (sizeof (UINT16) * 8)) - 1)
  #define SCT_BM_MAX_OPTION_NUMBER     ((1 << (sizeof (UINT16) * 8)) - 2)

  UINTN OptionType;
  
  #define SCT_BM_LO_BOOT          0
  #define SCT_BM_LO_KEY           1
  #define SCT_BM_LO_DRIVER        2
  #define SCT_BM_LO_SYSPREP       3  
  #define SCT_BM_LO_MAX_TYPE      4
  

  UINT32 Attributes;

  UINTN DescriptionLength;              // The number of bytes in Description.
  PCHAR16 Description;

  UINT16 FilePathListLength;
  EFI_DEVICE_PATH_PROTOCOL *FilePathList;
  UINTN NumberOfFilePaths;


  UINT32 OptionalDataLength;            // The number of bytes in OptionalData.
  PUINT8 OptionalData;

  //
  // RawData is the packed version of the above fields. The length in bytes and
  // the Crc from gBS->CalculateCrc32() are saved for convenience.
  //

  UINTN RawLength;                      // Number of bytes in RawData.
  PUINT8 RawData;                       // The byte-packed load option.
  UINT32 RawCrc;                        // Crc32 of the RawData.
  BOOLEAN InBootOrder;                  // Is this BootOption listed in BootOrder.

  //
  // Pointer to the next LOAD_OPTION object in the linked list of LOAD_OPTIONs.
  //

//  struct _LOAD_OPTION_OBJECT *Next;

} LOAD_OPTION_OBJECT, *PLOAD_OPTION_OBJECT;

CHAR16 *
UiDevicePathToStr (
  IN EFI_DEVICE_PATH_PROTOCOL     *DevPath
  )
{
  EFI_STATUS                       Status;
  CHAR16                           *ToText;
  EFI_DEVICE_PATH_TO_TEXT_PROTOCOL *DevPathToText;

  if (DevPath == NULL) {
    Print(L"Error.\n");
    return NULL;
  }

  Status = gBS->LocateProtocol (
                  &gEfiDevicePathToTextProtocolGuid,
                  NULL,
                  (VOID **) &DevPathToText
                  );
  if (EFI_ERROR (Status)) {
    Print(L"Error.\n");
    return NULL;
  }

  ToText = DevPathToText->ConvertDevicePathToText (
                            DevPath,
                            FALSE,
                            FALSE
                            );
//  ASSERT (ToText != NULL);
  return ToText;
}

#pragma pack(16)
typedef struct {
  UINT16 A;
  UINT16 B;
  UINT16 C;
  UINT32 D;
} DATA;

typedef struct {
  UINT16 A;
  UINT32 B;
  UINT16 C;
  CHAR8 D;
} DATA2;
#pragma pack()

VOID
DumpBuffer (
  UINT8 *Buffer,
  UINT32 Length,
  UINT8 * ShowStr OPTIONAL
  )
{
  UINT32 ip, jp;
  UINT8 cc;
  if (ShowStr != NULL) {
    DEBUG ((EFI_D_ERROR, "Dump %a Address = 0x%x  Size = 0x%x\n", ShowStr, Buffer, Length));
    Print (L"Dump %a Address = 0x%x  Size = 0x%x\n", ShowStr, Buffer, Length);
  } else {
    DEBUG ((EFI_D_ERROR, "Dump Address = 0x%x  Size = 0x%x\n", Buffer, Length));
    Print (L"Dump Address = 0x%x  Size = 0x%x\n", Buffer, Length);
  }  

  for (ip = 1 ; ip <= Length ; ip += 16) {
    if ((ip <= 512) || ip > (Length - 512)) {
      DEBUG ((EFI_D_ERROR, "%04x: ", (ip - 1)));
      for (jp = 0 ; jp < 16 ; jp ++) {
        if ((ip + jp) <= Length) {
          DEBUG ((EFI_D_ERROR, "%02x ", Buffer [ip + jp - 1]));
          Print(L"%02x ", Buffer [ip + jp - 1]);
        } else {
          DEBUG ((EFI_D_ERROR, "   "));
          Print(L"   ");
        }
      }
      for (jp = 0 ; jp < 16 ; jp ++) {
        if ((ip + jp) <= Length) {
          cc = Buffer [ip + jp - 1];
          if (cc >= 0x20 && cc <= 0x7E) {
            DEBUG ((EFI_D_ERROR, "%c", cc));
            Print (L"%c", cc);
          } else  {
            DEBUG ((EFI_D_ERROR, "."));
            Print (L".");
          }
        } else {
          DEBUG ((EFI_D_ERROR, "  "));
          Print(L"  ");
        }
      }
      DEBUG ((EFI_D_ERROR, "\n"));
      Print(L"\n");
    } else {
      if ((ip % 512) == 1) {
        DEBUG ((EFI_D_ERROR, "%04x: ...\n", (ip - 1)));
        Print(L"%04x: ...\n", (ip - 1));
      }
    }
  }
} // DumpBuffer


/**
  The user Entry Point for Application. The user code starts with this function
  as the real entry point for the application.

  @param[in] ImageHandle    The firmware allocated handle for the EFI image.  
  @param[in] SystemTable    A pointer to the EFI System Table.
  
  @retval EFI_SUCCESS       The entry point is executed successfully.
  @retval other             Some error occurs when executing this entry point.

**/
EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_DEVICE_PATH  *DP;
  EFI_STATUS      Status;
  EFI_HANDLE      NewHandle;
  UINTN           ExitDataSizePtr; 
  
  DP=FileDevicePath(NULL,L"fso:\\fake.efi");
//  Print(L"%s\n",ConvertDevicePathToText(DP,TRUE,FALSE));

  //
  // Load the image with:
  // FALSE - not from boot manager and NULL, 0 being not already in memory
  //
  Status = gBS->LoadImage(
                  FALSE,
                  ImageHandle,
                  DP,
                  (VOID*)&Hello2_efi[0],
                  sizeof(Hello2_efi),
                  &NewHandle);     
  if (EFI_ERROR(Status)) {
          Print(L"Load image Error!\n");
          return 0;
  }
  Print(L"Hello2_efi = 0x%x  NewHandle = 0x%x\n", Hello2_efi, NewHandle);
  DumpBuffer ( (UINT8 *)Hello2_efi, 0x30, NULL);
  DumpBuffer ( (UINT8 *)NewHandle, 0x50, NULL);
  //
  // now start the image, passing up exit data if the caller requested it
  //
  Status = gBS->StartImage(
               NewHandle,
               &ExitDataSizePtr,
               NULL
        );
  if (EFI_ERROR(Status)) {
          Print(L"\nError during StartImage [%r]\n",Status);
          return 0;
  }   
  
  // Status = gBS->StartImage(
  //              NewHandle,
  //              &ExitDataSizePtr,
  //              NULL
  //       );
  // if (EFI_ERROR(Status)) {
  //         Print(L"\nError during StartImage [%r]\n",Status);
  //         return 0;
  // }  
  
  Print(L"---------------------\n");  
  Status = gBS->UnloadImage(NewHandle);    
  DumpBuffer ( (UINT8 *)NewHandle, 0x50, NULL);

  if (EFI_ERROR(Status)) {
          Print(L"Un-Load image Error! %r\n",Status);
          return 0;
  }        
  
  return EFI_SUCCESS;
}