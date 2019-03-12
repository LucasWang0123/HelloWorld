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

/*  EFI_STATUS               Status;
  DATA                     *Lucas;
  Status = gBS->AllocatePool(EfiLoaderData, 0xC, (void **)&Lucas);
  
  Lucas->A = 0x5555;
  Lucas->B = 0x6666;
  Lucas->C = 0x7777;
  Lucas->D = 0x88888888;
  Print(L"=> 0x%x\n", *((UINT8 *)((UINTN)(0xFE00DA26))));
  Print(L"sizeof(UINTN) = 0x%x\n", sizeof(UINTN));

  Print(L"=> 0x%x \n", &Lucas);
  Print(L"=> 0x%x \n", Lucas);
  Print(L"=> 0x%x\n", ((DATA *) Lucas)->A);
  Print(L"=> 0x%x\n", ((DATA *) Lucas)->B);  
  Print(L"=> 0x%x\n", ((DATA *) Lucas)->C);
  Print(L"=> 0x%x\n", ((DATA *) Lucas)->D);
  Print(L"=> 0x%x\n", &(((DATA *) Lucas)->A));
  Print(L"=> 0x%x\n", &(((DATA *) Lucas)->B));
  Print(L"=> 0x%x\n", &(((DATA *) Lucas)->C));
  Print(L"=> 0x%x\n", &(((DATA *) Lucas)->D));

  Print(L"sizeof(DATA) => 0x%x\n", sizeof(DATA));

  Print(L"=> 0x%x\n", ((DATA2 *) Lucas)->A);
  Print(L"=> 0x%x\n", ((DATA2 *) Lucas)->B);
  Print(L"=> 0x%x\n", ((DATA2 *) Lucas)->C);  
  Print(L"=> 0x%x\n", ((DATA2 *) Lucas)->D);
  Print(L"=> 0x%x\n", &(((DATA2 *) Lucas)->A));
  Print(L"=> 0x%x\n", &(((DATA2 *) Lucas)->B));
  Print(L"=> 0x%x\n", &(((DATA2 *) Lucas)->C));
  Print(L"=> 0x%x\n", &(((DATA2 *) Lucas)->D));
  Print(L"sizeof(DATA2) => 0x%x\n", sizeof(DATA2));

  gBS->FreePool (Lucas);
*/
/*
  int *aaa;
  char *bbb;
  int address = 111;
  aaa =  &address;
  bbb = (char *) aaa;
  DEBUG ((EFI_D_INFO, "sizeof(aaa) = %d  aaa = 0x%016lx aaa = 0x%016llx aaa = 0x%x aaa = %d \n", sizeof(int *), sizeof(*aaa), aaa, *aaa, *aaa));  
  DEBUG ((EFI_D_INFO, "sizeof(bbb) = %d  bbb = 0x%016lx bbb = 0x%016llx bbb = 0x%x bbb = %d \n", sizeof(*bbb), bbb, bbb, *bbb, *bbb));    

  EFI_CPU_ARCH_PROTOCOL   *CpuArch;
  EFI_STATUS               Status;
  UINTN                    *map_buf;
  UINTN                    map_size;

  Status = gBS->LocateProtocol (&gEfiCpuArchProtocolGuid, NULL, (VOID **) &CpuArch);


  map_size = sizeof(*map_buf) * 31;   
  Print(L"sizeof(*map_buf)*31 = %d\n", map_size);

  Status = gBS->AllocatePool(EfiLoaderData, map_size, (void **)&map_buf); 
  if (Status != EFI_SUCCESS) { 
      Print(L"ERROR: Failed to allocate pool for memory map\n"); 
      return Status; 
  } 

  Print(L"Status = %r  map_buf = 0x%llx\n", Status, map_buf); 
 */

/*   Status = CpuArch->SetMemoryAttributes (
                      CpuArch,
                      0x80000000,
                      0x10000000,
                      EFI_MEMORY_WC
                      );
   Print(L"Status = %r\n", Status);     
*/

  UINT32 i;
  EFI_STATUS                 Status;
//  EFI_USB2_HC_PROTOCOL       *Usb2HCProtocol;
//  EFI_DEVICE_PATH_PROTOCOL   *RemainingDevicePath;
  EFI_DEVICE_PATH_PROTOCOL   *DevicePathProtocol;
  EFI_PCI_IO_PROTOCOL        PciIoProtocol;

  UINTN                      BufferSize;
  EFI_HANDLE                 *HandleBuffer;
//  EFI_HANDLE                 Device;
  UINTN                      NumberOfHandles;
//  CHAR16                     *Line;  //Lucas DEBUG
  CHAR8  *String = "HelloWorld comes from";
  CHAR16 *NewString;

  DumpBuffer ( (CHAR8 *)String, 30 , NULL);
  
  NewString = (CHAR16 *)AllocatePool ( 30 * sizeof(CHAR16) );
  DumpBuffer ( (CHAR8 *)NewString, 30 * sizeof(CHAR16), NULL);

  ZeroMem ( NewString, 30 * sizeof(CHAR16) );
  DumpBuffer ( (CHAR8 *)NewString, 30 * sizeof(CHAR16), NULL);

  AsciiStrToUnicodeStr(String,NewString);
  DumpBuffer ( (CHAR8 *)NewString, 30 * sizeof(CHAR16), NULL);

  Print(L"%s\n", String); 
  Print(L"%s\n", NewString);
 

//  Line = AllocateZeroPool (100 * sizeof (CHAR16));
  i = 0;
  BufferSize = 0;
  HandleBuffer = NULL;

/*  Status = gBS->LocateProtocol (&gEfiUsb2HcProtocolGuid, NULL, (VOID**)&UsbIo);
  Status = gBS->LocateHandle (
                    ByProtocol,
                    &gEfiUsb2HcProtocolGuid,
                    NULL,
                    &BufferSize,
                    HandleBuffer);

    Print(L"Status = %r  HandleBuffer = 0x%x HandleBuffer[0]=0x%x HandleBuffer[1]=0x%x\n",Status,HandleBuffer,HandleBuffer[0],HandleBuffer[1]);
    
    if (EFI_ERROR (Status)) {
      Print(L"Error.\n");
      return EFI_SUCCESS;
    }
*/
 
//  Print(L"Status = %r  Size = %d\n",Status,BufferSize / sizeof (EFI_HANDLE));
//  gST->ConOut->ClearScreen (gST->ConOut);
//  gST->ConOut->SetAttribute (gST->ConOut, EFI_BLUE | EFI_BACKGROUND_BLACK);

  Status = gBS->LocateHandleBuffer (
                   ByProtocol,
                   &gEfiPciIoProtocolGuid,
                   NULL,
                   &NumberOfHandles,
                   &HandleBuffer
                   );

  Print(L"Status = %r  Size = %d\n",Status,NumberOfHandles);
  

  for (i = 0; i < NumberOfHandles; i++) {
//    Print(L"HandleBuffer[%d] = 0x%x \n",i,HandleBuffer[i]);

    Status = gBS->HandleProtocol (HandleBuffer [i], &gEfiDevicePathProtocolGuid, (VOID**)&DevicePathProtocol);
//    Print(L"Status = %r  DevicePath = %s\n", Status, UiDevicePathToStr(DevicePathProtocol));
    Status = gBS->HandleProtocol (HandleBuffer [i], &gEfiPciIoProtocolGuid, (VOID**)&PciIoProtocol);
//    Print(L"Status = %r  \n", Status);
/*
    if (!EFI_ERROR (Status)) {
      for(j=0 ;j<99 ;j++){
        Line[j] = 0x41;
      } 
      Line[99] = L'\0';  
      ConOut->OutputString (ConOut, Line);
    }

    Status = gBS->LocateDevicePath (
                    &gEfiPciIoProtocolGuid,
                    &RemainingDevicePath,
                    &Device);
    Print(L"Device = 0x%x    %s\n", Device, UiDevicePathToStr(RemainingDevicePath));

    Status = gBS->UninstallProtocolInterface (HandleBuffer [i], &gEfiUsbIoProtocolGuid, UsbIoProtocol);
    if (EFI_ERROR (Status)) {
      Print(L"Error.\n");
      return EFI_SUCCESS;
    }
*/

  }
  gBS->FreePool (HandleBuffer);
  return EFI_SUCCESS;
}
