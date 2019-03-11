#include <Uefi.h>
#include <Library/PcdLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
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
  { 0x8BE4DF61, 0x93CA, 0x11d2, {0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C } }

#define SCT_BDS_SERVICES_PROTOCOL_GUID \
  { 0xb7646a4, 0x6b44, 0x4332, 0x85, 0x88, 0xc8, 0x99, 0x81, 0x17, 0xf2, 0xef }

typedef CHAR8 * PCHAR8;                 // pointer to an 8-bit character.
typedef CHAR16 * PCHAR16;                // pointer to a 16-bit character.
typedef UINT8 * PUINT8;                 // pointer to an unsigned 8-bit value.

typedef struct _LOAD_OPTION_OBJECT {

  UINT16 OptionNumber;

  #define SCT_BM_INVALID_OPTION_NUMBER ((1 << (sizeof (UINT16) * 8)) - 1)
  #define SCT_BM_MAX_OPTION_NUMBER     ((1 << (sizeof (UINT16) * 8)) - 2)

  //
  // This field indicates if the option is a Boot or Driver option.
  // Application is a sub-type of Boot.
  //

  UINTN OptionType;

  //
  // If want to add more Boot type, please add number to SCT_BM_LO_MAX_TYPE.
  //
  
  #define SCT_BM_LO_BOOT          0
  #define SCT_BM_LO_KEY           1
  #define SCT_BM_LO_DRIVER        2
  #define SCT_BM_LO_SYSPREP       3  
  #define SCT_BM_LO_MAX_TYPE      4
  
  //
  // The following fields mirror the UEFI specified structure for load options.
  // In the UEFI specification these fields are packed together and require
  // math to access, especially the later fields. These are unpacked into this
  // structure for ease of use. The fields are not in the same order as the
  // specification and additional fields have been added for the lengths of the
  // array-style fields, FilePathList, Description, OptionalData.
  //

  UINT32 Attributes;

  UINTN DescriptionLength;              // The number of bytes in Description.
  PCHAR16 Description;

  UINT16 FilePathListLength;
  EFI_DEVICE_PATH_PROTOCOL *FilePathList;
  UINTN NumberOfFilePaths;

  //
  // OptionalDataLength is UINT32 to match ImageInfo->LoadOptionsSize's type.
  //

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
  } else {
    DEBUG ((EFI_D_ERROR, "Dump Address = 0x%x  Size = 0x%x\n", Buffer, Length));
  }  

  for (ip = 1 ; ip <= Length ; ip += 16) {
    if ((ip <= 512) || ip > (Length - 512)) {
      DEBUG ((EFI_D_ERROR, "%04x: ", (ip - 1)));
      for (jp = 0 ; jp < 16 ; jp ++) {
        if ((ip + jp) <= Length) {
          DEBUG ((EFI_D_ERROR, "%02x ", Buffer [ip + jp - 1]));
        } else {
          DEBUG ((EFI_D_ERROR, "   "));
        }
      }
      for (jp = 0 ; jp < 16 ; jp ++) {
        if ((ip + jp) <= Length) {
          cc = Buffer [ip + jp - 1];
          if (cc >= 0x20 && cc <= 0x7E) {
            DEBUG ((EFI_D_ERROR, "%c", cc));
          } else  {
            DEBUG ((EFI_D_ERROR, "."));
          }
        } else {
          DEBUG ((EFI_D_ERROR, "  "));
        }
      }
      DEBUG ((EFI_D_ERROR, "\n"));
    } else {
      if ((ip % 512) == 1) {
        DEBUG ((EFI_D_ERROR, "%04x: ...\n", (ip - 1)));
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
  EFI_STATUS Status;
//  EFI_GUID gEfiGlobalVariableGuid = EFI_GLOBAL_VARIABLE;
  EFI_GUID gSctBdsServicesProtocolGuid = SCT_BDS_SERVICES_PROTOCOL_GUID;
//  CHAR16 VariableName [12] =  L"BootOrderDefault"; 
  UINT32 TempAttributes;
  UINTN TempDataSize;
  VOID *TempData;
  PLOAD_OPTION_OBJECT Option;
//  PUINT8 p;
//  UINTN DescriptionLength;

  Option = AllocateZeroPool (sizeof (LOAD_OPTION_OBJECT));
//  VariableName =  L"Boot0011";
  TempDataSize = 0;
  TempData = NULL;

  Status = gBS->AllocatePool (EfiBootServicesData, TempDataSize, &TempData);
  TempDataSize = 50;
  Status = gRT->GetVariable (
                  L"BootOrderDefault",
                  (EFI_GUID *) &gSctBdsServicesProtocolGuid,
                  &TempAttributes,      // OUT Attributes.
                  &TempDataSize,        // IN OUT DataSize.
                  TempData);            // OUT Data.

  Print(L"Status = %r After gRT->GetVariable\n", Status);

  if (TempDataSize > 0) {
    Status = gBS->AllocatePool (EfiBootServicesData, TempDataSize, &TempData);
    Print(L"TempDataSize = %d\n", TempDataSize);
    Status = gRT->GetVariable (
                  L"BootOrderDefault",
                  (EFI_GUID *) &gSctBdsServicesProtocolGuid,
                  &TempAttributes,      // OUT Attributes.
                  &TempDataSize,        // IN OUT DataSize.
                  TempData);            // OUT Data.
  }

  // p = TempData;
  // p += sizeof (UINT32);
  // p += sizeof (UINT16);
  // DescriptionLength = StrSize ((PCHAR16)p);
  // Print(L"DescriptionLength = %d\n", DescriptionLength);
  // Option->Description = AllocateCopyPool (DescriptionLength, p);
  // Print(L"Option->Description = %s\n", Option->Description);

  return EFI_SUCCESS;
}

 /* 
 #define Lucas 100
EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
     DEBUG ((DEBUG_INFO, "start 0220 Lucas = %d\n", Lucas));
   if(0){
    #undef Lucas
    #define Lucas           1000000  ///< 1sec timeout in microseconds
  }
  DEBUG ((DEBUG_INFO, "end 0220 Lucas = %d\n", Lucas));
/*  EFI_STATUS               Status;
  DATA                     *Lucas;
  Status = gBS->AllocatePool(EfiLoaderData, 0xC, (void **)&Lucas); 
  if (Status != EFI_SUCCESS) { 
      Print(L"ERROR: Failed to allocate pool for memory map\n"); 
      return Status; 
  } 

  Print(L"       GetVariable            @[0x%x] %d\n",gRT->GetVariable , sizeof(int) );
  gRT->GetVariable = (EFI_GET_VARIABLE)((UINT64)0x760bc9e0);
  Print(L"       GetVariable            @[0x%x]\n",gRT->GetVariable);
  
  Lucas->A = 0x5555;
  Lucas->B = 0x6666;
  Lucas->C = 0x7777;
  Lucas->D = 0x88888888;
  Print(L"=> 0x%x\n", *((UINT8 *)((UINTN)(0xFE00DA26))));
  Print(L"sizeof(UINTN) = 0x%x\n", sizeof(UINTN));

#define OPTION_PLANAR_ID_SUPPORT 1
#if OPTION_PLANAR_ID_SUPPORT
  Print(L"Address value = 0x%x\n", (Mmio8 ((UINTN)(CONFIG_FCH_MMIO_BASE + 0x1500 + (11 << 2) + 2))));
#endif*/  
/*  Print(L"=> 0x%x \n", &Lucas);
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
*/
//  gBS->FreePool (Lucas);

/*
  int *aaa;
  char *bbb;
  int address = 111;
  aaa =  &address;
  bbb = (char *) aaa;
  DEBUG ((EFI_D_INFO, "sizeof(aaa) = %d  aaa = 0x%016lx aaa = 0x%016llx aaa = 0x%x aaa = %d \n", sizeof(int *), sizeof(*aaa), aaa, *aaa, *aaa));  
  DEBUG ((EFI_D_INFO, "sizeof(bbb) = %d  bbb = 0x%016lx bbb = 0x%016llx bbb = 0x%x bbb = %d \n", sizeof(*bbb), bbb, bbb, *bbb, *bbb));    

/*  EFI_CPU_ARCH_PROTOCOL   *CpuArch;
  EFI_STATUS               Status;
//  UINTN                    *map_buf;
//  UINTN                    map_size;

  Status = gBS->LocateProtocol (&gEfiCpuArchProtocolGuid, NULL, (VOID **) &CpuArch);

/*
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
/*	UINT32 Index;
  UINT32 i,j;
	EFI_STATUS                 Status;
//  EFI_USB2_HC_PROTOCOL       *Usb2HCProtocol;
  EFI_DEVICE_PATH_PROTOCOL   *RemainingDevicePath;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *ConOut;
/*  EFI_GRAPHICS_OUTPUT_PROTOCOL  *GraphicsOutput;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL BltBuffer[5] = {{0x30, 0x30, 0x30, 0},
                                                {0x30, 0x00, 0x00, 0},
                                                {0x00, 0x30, 0x00, 0},
                                                {0x00, 0x00, 0x30, 0},
                                                {0x30, 0x30, 0x30, 0}};*/
/*  UINTN                      BufferSize;
  EFI_HANDLE                 *HandleBuffer;
//  EFI_HANDLE                 Device;
  UINTN                      NumberOfHandles;
  CHAR16                      *Line;//Lucas DEBUG
//  UINTN                      Columns;
//  UINTN                      Rows;
  Line = AllocateZeroPool (100 * sizeof (CHAR16));
  i = 0;
  j = 0;
	Index = 0;
  BufferSize = 0;
  HandleBuffer = NULL;
//  Columns = 0;
//  Rows = 0;
//  Status = gBS->LocateProtocol (&gEfiUsb2HcProtocolGuid, NULL, (VOID**)&UsbIo);
/*  Status = gBS->LocateHandle (
                    ByProtocol,
                    &gEfiUsb2HcProtocolGuid,
                    NULL,
                    &BufferSize,
                    HandleBuffer);

  if (Status == EFI_BUFFER_TOO_SMALL) {
    Status = gBS->AllocatePool (
                EfiBootServicesData,
                BufferSize,
                (VOID**)&HandleBuffer);
//    Print(L"Status = %r  HandleBuffer = 0x%x HandleBuffer[0]=0x%x HandleBuffer[1]=0x%x\n",Status,HandleBuffer,HandleBuffer[0],HandleBuffer[1]);
    if (EFI_ERROR (Status)) {
      Print(L"Error.\n");
      return EFI_SUCCESS;
    }
*/
/*    Status = gBS->LocateHandle (
                  ByProtocol,
                  &gEfiUsb2HcProtocolGuid,
                  NULL,
                  &BufferSize,
                  HandleBuffer);
//    Print(L"Status = %r  HandleBuffer = 0x%x HandleBuffer[0]=0x%x HandleBuffer[1]=0x%x\n",Status,HandleBuffer,HandleBuffer[0],HandleBuffer[1]);
    if (EFI_ERROR (Status)) {
      Print(L"Error.\n");
      return EFI_SUCCESS;
    }
  }

  Print(L"Status = %r  Size = %d\n",Status,BufferSize / sizeof (EFI_HANDLE));
*/
//  gST->ConOut->ClearScreen (gST->ConOut);
//  gST->ConOut->SetAttribute (gST->ConOut, EFI_BLUE | EFI_BACKGROUND_BLACK);
/*  Status = gST->ConOut->QueryMode (
               gST->ConOut,
               gST->ConOut->Mode->Mode,
               &Columns,
               &Rows);
  
  //Rows = Rows - 5;
  //gST->ConOut->SetCursorPosition (gST->ConOut, 165, Rows);
//  gST->ConOut->OutputString (gST->ConOut, Columns);
  //Print(L"Status = %r Columns = %d Rows = %d gST->ConOut->Mode->MaxMode = %d \n", Status, Columns, Rows, gST->ConOut->Mode->Mode);
*/  

/*  Status = gBS->LocateHandleBuffer (
                   ByProtocol,
                   &gEfiSimpleTextOutProtocolGuid,
                   NULL,
                   &NumberOfHandles,
                   &HandleBuffer
                   );

  Print(L"Status = %r  Size = %d\n",Status,NumberOfHandles);


//  Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **) &GraphicsOutput);
//  Status = GraphicsOutput->Blt(GraphicsOutput, BltBuffer, EfiBltVideoFill, 0, 0, 0, 0, GraphicsOutput->Mode->Info->HorizontalResolution, GraphicsOutput->Mode->Info->VerticalResolution, 0);
  


  for (i = 0; i < NumberOfHandles; i++) {
    //Print(L"HandleBuffer[%d] = 0x%x \n",i,HandleBuffer[i]);
/*    Status = gBS->HandleProtocol (HandleBuffer [i], &gEfiGraphicsOutputProtocolGuid, (VOID**)&GraphicsOutput);
    Status = GraphicsOutput->Blt(GraphicsOutput, &BltBuffer[i], EfiBltVideoFill, 0, 0, 0, 0, GraphicsOutput->Mode->Info->HorizontalResolution, GraphicsOutput->Mode->Info->VerticalResolution, 0);
    Print(L"Status = %r\n", Status);
    Print(L"Current mode =[%d] \n",GraphicsOutput->Mode->Mode);
    Print(L"Screen Width =[%d] \n",GraphicsOutput->Mode->Info->HorizontalResolution);
    Print(L"Screen height=[%d] \n",GraphicsOutput->Mode->Info->VerticalResolution);
    gBS->Stall(5000000);
/*    if (EFI_ERROR (Status)) {
      Print(L"Error.\n");
      return EFI_SUCCESS;
    }
*/
/*    Status = gBS->HandleProtocol (HandleBuffer [i], &gEfiDevicePathProtocolGuid, (VOID**)&RemainingDevicePath);
    Print(L"%s\n", UiDevicePathToStr(RemainingDevicePath));
    Status = gBS->HandleProtocol (HandleBuffer [i], &gEfiSimpleTextOutProtocolGuid, (VOID**)&ConOut);
    DEBUG ((DEBUG_INFO, "0125 Lucas Status = %r  i = %d\n", Status, i));
    if (!EFI_ERROR (Status)) {
      for(j=0 ;j<99 ;j++){
        Line[j] = 0x41;
      } 
      Line[99] = L'\0';  
      ConOut->OutputString (ConOut, Line);
    }
    //Print(L"Status = %r\n", Status);
/*    Status = gBS->LocateDevicePath (
                    &gEfiPciIoProtocolGuid,
                    &RemainingDevicePath,
                    &Device);
    Print(L"Device = 0x%x    %s\n", Device, UiDevicePathToStr(RemainingDevicePath));
*/

/*    Status = gBS->UninstallProtocolInterface (HandleBuffer [i], &gEfiUsbIoProtocolGuid, UsbIoProtocol);
    if (EFI_ERROR (Status)) {
      Print(L"Error.\n");
      return EFI_SUCCESS;*/
  //  }

 // gBS->FreePool (HandleBuffer);
//  return EFI_SUCCESS;
//}
