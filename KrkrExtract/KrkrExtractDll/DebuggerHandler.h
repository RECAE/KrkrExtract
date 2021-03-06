#pragma once

#include <my.h>
#include "KrkrHeaders.h"
#include "XP3Parser.h"
#include "getopt.h"
#include "KrkrExtract.h"
#include "zlib.h"
#include "MyLib.h"
#include "ml.h"

#pragma comment(lib, "getopt.lib")


PVOID GetTVPCreateStreamCall_Debugger()
{
	PVOID CallIStreamStub, CallIStream, CallTVPCreateStreamCall;
	ULONG OpSize, OpOffset;
	WORD  WordOpcode;

	static char funcname[] = "IStream * ::TVPCreateIStream(const ttstr &,tjs_uint32)";

	LOOP_ONCE
	{
		CallTVPCreateStreamCall = NULL;

		CallIStreamStub = TVPGetImportFuncPtr(funcname);
		if (!CallIStreamStub)
			break;

		CallIStream = NULL;
		OpOffset    = 0;

		LOOP_FOREVER
		{
			if (((PBYTE)CallIStreamStub + OpOffset)[0] == 0xCC)
				break;

			WordOpcode = *(PWORD)((ULONG_PTR)CallIStreamStub + OpOffset);
			//mov edx,dword ptr [ebp+0xC]
			if (WordOpcode == 0x558B)
			{
				OpOffset += 2;
				if (((PBYTE)CallIStreamStub + OpOffset)[0] == 0xC)
				{
					OpOffset++;
					WordOpcode = *(PWORD)((ULONG_PTR)CallIStreamStub + OpOffset);
					//mov edx,dword ptr [ebp+0x8]
					if (WordOpcode == 0x4D8B)
					{
						OpOffset += 2;
						if (((PBYTE)CallIStreamStub + OpOffset)[0] == 0x8)
						{
							OpOffset++;
							if (((PBYTE)CallIStreamStub + OpOffset)[0] == CALL)
							{
								CallIStream = (PVOID)GetCallDestination(((ULONG_PTR)CallIStreamStub + OpOffset));
								OpOffset += 5;
								break;
							}
						}
					}
				}
			}
			//the next opcode
			OpSize = GetOpCodeSize32(((PBYTE)CallIStreamStub + OpOffset));
			OpOffset += OpSize;
		}

		if (!CallIStream)
			break;

		OpOffset = 0;
		LOOP_FOREVER
		{
			if (((PBYTE)CallIStream + OpOffset)[0] == 0xC3)
				break;

			//find the first call
			if (((PBYTE)CallIStream + OpOffset)[0] == CALL)
			{
				CallTVPCreateStreamCall = (PVOID)GetCallDestination(((ULONG_PTR)CallIStream + OpOffset));
				OpOffset += 5;
				break;
			}

			//the next opcode
			OpSize = GetOpCodeSize32(((PBYTE)CallIStream + OpOffset));
			OpOffset += OpSize;
		}

		LOOP_FOREVER
		{
			if (((PBYTE)CallIStream + OpOffset)[0] == 0xC3)
				break;

			if (((PBYTE)CallIStream + OpOffset)[0] == CALL)
			{
				//push 0xC
				//call HostAlloc
				//add esp, 0x4
				if (((PBYTE)CallIStream + OpOffset - 2)[0] == 0x6A &&
					((PBYTE)CallIStream + OpOffset - 2)[1] == 0x0C)
				{
					GlobalData::GetGlobalData()->StubHostAlloc = (FuncHostAlloc)GetCallDestination(((ULONG_PTR)CallIStream + OpOffset));
					OpOffset += 5;
				}
				break;
			}

			//the next opcode
			OpSize = GetOpCodeSize32(((PBYTE)CallIStream + OpOffset));
			OpOffset += OpSize;
		}

		LOOP_FOREVER
		{
			if (((PBYTE)CallIStream + OpOffset)[0] == 0xC3)
				break;

			//mov eax, mem.offset
			if (((PBYTE)CallIStream + OpOffset)[0] == 0xC7 &&
				((PBYTE)CallIStream + OpOffset)[1] == 0x00)
			{
				OpOffset += 2;
				GlobalData::GetGlobalData()->IStreamAdapterVtable = *(PULONG_PTR)((PBYTE)CallIStream + OpOffset);
				OpOffset += 4;
				break;
			}

			//the next opcode
			OpSize = GetOpCodeSize32(((PBYTE)CallIStream + OpOffset));
			OpOffset += OpSize;
		}
	}


	//Find virtual table offset
	//IStreamAdapter

	if (GlobalData::GetGlobalData()->StubHostAlloc && GlobalData::GetGlobalData()->IStreamAdapterVtable)
	{
		//PrintConsoleW(L"Analyze ok...\n");
		return CallTVPCreateStreamCall;
	}
	else
	{
		return NULL;
	}
}


tTJSBinaryStream* FASTCALL CallTVPCreateStream_Debugger(const ttstr& FilePath)
{
	tTJSBinaryStream* Stream;
	GlobalData*       Handle;

	Handle = GlobalData::GetGlobalData();

	if (Handle->StubTVPCreateStream == NULL)
		Handle->StubTVPCreateStream = (FuncCreateStream)GetTVPCreateStreamCall_Debugger();

	Stream = NULL;

	if (Handle->StubTVPCreateStream == NULL)
		return Stream;

	return Handle->StubTVPCreateStream(FilePath, TJS_BS_READ);
}


IStream* FASTCALL ConvertBStreamToIStream_Debugger(tTJSBinaryStream* BStream)
{
	IStream*  Stream;
	PVOID     CallHostAlloc;
	ULONG_PTR IStreamAdapterVTableOffset;

	CallHostAlloc = GlobalData::GetGlobalData()->StubHostAlloc;
	IStreamAdapterVTableOffset = GlobalData::GetGlobalData()->IStreamAdapterVtable;
	Stream = NULL;

	INLINE_ASM
	{
		push 0xC;
		call CallHostAlloc;
		add  esp, 0x4;
		test eax, eax;
		jz   NO_CREATE_STREAM;
		mov  esi, IStreamAdapterVTableOffset;
		mov  dword ptr[eax], esi; //Vtable 
		mov  esi, BStream;
		mov  dword ptr[eax + 4], esi; //StreamHolder
		mov  dword ptr[eax + 8], 1;   //ReferCount
		mov  Stream, eax;

	NO_CREATE_STREAM:
	}

	return Stream;
}


ForceInline NTSTATUS FASTCALL DumpXP3ArchiveIndex(LPCWSTR lpFileName, NtFileDisk& file)
{
	NTSTATUS                Status;
	NtFileDisk              File;
	GlobalData*             Handle;
	ULONG                   Count;
	KRKR2_XP3_HEADER        XP3Header;
	KRKR2_XP3_DATA_HEADER   DataHeader;
	PBYTE                   Indexdata;
	LARGE_INTEGER           BeginOffset, Offset;
	CHAR                    M2ChunkInfo[8];
	WCHAR                   IndexFileName[MAX_PATH];


	Handle = GlobalData::GetGlobalData();

	Count = 0;
	BeginOffset.QuadPart = 0;
	Status = file.Read(&XP3Header, sizeof(XP3Header));
	RtlZeroMemory(M2ChunkInfo, sizeof(M2ChunkInfo));

	if (NT_FAILED(Status))
	{
		if (Handle->DebugOn)
			PrintConsoleW(L"Invaild Package\n");

		return Status;
	}

	if ((*(PUSHORT)XP3Header.Magic) == IMAGE_DOS_SIGNATURE)
	{
		Status = FindEmbededXp3OffsetSlow(file, &BeginOffset);

		if (NT_FAILED(Status))
		{
			if (Handle->DebugOn)
				PrintConsoleW(L"No an embeded Package %p\n", Status);

			return Status;
		}

		if (Handle->DebugOn)
			PrintConsoleW(L"Embeded Package %p\n", BeginOffset.LowPart);

		file.Seek(BeginOffset, FILE_BEGIN);
		file.Read(&XP3Header, sizeof(XP3Header));
	}
	else
	{
		BeginOffset.QuadPart = 0;
	}

	Status = STATUS_UNSUCCESSFUL;

	if (RtlCompareMemory(StaticXP3V2Magic, XP3Header.Magic, sizeof(StaticXP3V2Magic)) != sizeof(StaticXP3V2Magic))
	{
		if (Handle->DebugOn)
			PrintConsoleW(L"No a XP3 Package!\n");

		return Status;
	}

	ULONG64 CompresseBufferSize = 0x1000;
	ULONG64 DecompressBufferSize = 0x1000;
	PBYTE   pCompress = (PBYTE)AllocateMemoryP((ULONG)CompresseBufferSize);
	PBYTE   pDecompress = (PBYTE)AllocateMemoryP((ULONG)DecompressBufferSize);
	DataHeader.OriginalSize = XP3Header.IndexOffset;

	if (Handle->DebugOn)
		PrintConsoleW(L"Index Offset %08x\n", (ULONG32)XP3Header.IndexOffset.QuadPart);

	BOOL Result = FALSE;
	do
	{
		Offset.QuadPart = DataHeader.OriginalSize.QuadPart + BeginOffset.QuadPart;
		file.Seek(Offset, FILE_BEGIN);
		if (NT_FAILED(file.Read(&DataHeader, sizeof(DataHeader))))
		{
			if (Handle->DebugOn)
				PrintConsoleW(L"Couldn't Read Index Header\n");

			return Status;
		}

		if (DataHeader.ArchiveSize.HighPart != 0 || DataHeader.ArchiveSize.LowPart == 0)
			continue;

		if (DataHeader.ArchiveSize.LowPart > CompresseBufferSize)
		{
			CompresseBufferSize = DataHeader.ArchiveSize.LowPart;
			pCompress = (PBYTE)ReAllocateMemoryP(pCompress, (ULONG)CompresseBufferSize);
		}

		if ((DataHeader.bZlib & 7) == 0)
		{
			Offset.QuadPart = -8;
			file.Seek(Offset, FILE_CURRENT);
		}

		file.Read(pCompress, DataHeader.ArchiveSize.LowPart);

		BOOL EncodeMark = DataHeader.bZlib & 7;

		if (GlobalData::GetGlobalData()->DebugOn)
			PrintConsoleW(L"Index Encode %x\n", DataHeader.bZlib);

		if (EncodeMark == FALSE)
		{
			if (GlobalData::GetGlobalData()->DebugOn)
				PrintConsoleW(L"Index : Raw Data\n");

			if (DataHeader.ArchiveSize.LowPart > DecompressBufferSize)
			{
				DecompressBufferSize = DataHeader.ArchiveSize.LowPart;
				pDecompress = (PBYTE)ReAllocateMemoryP(pDecompress, (ULONG)DecompressBufferSize);
			}

			RtlCopyMemory(pDecompress, pCompress, DataHeader.ArchiveSize.LowPart);
			DataHeader.OriginalSize.LowPart = DataHeader.ArchiveSize.LowPart;
		}
		else
		{
			if (GlobalData::GetGlobalData()->DebugOn)
				PrintConsoleW(L"Index : Zlib Data\n");

			if (DataHeader.OriginalSize.LowPart > DecompressBufferSize)
			{
				DecompressBufferSize = DataHeader.OriginalSize.LowPart;
				pDecompress = (PBYTE)ReAllocateMemoryP(pDecompress, (ULONG)DecompressBufferSize);
			}

			DataHeader.OriginalSize.HighPart = DataHeader.OriginalSize.LowPart;
			if (uncompress((PBYTE)pDecompress, (PULONG)&DataHeader.OriginalSize.HighPart,
				(PBYTE)pCompress, DataHeader.ArchiveSize.LowPart) == Z_OK)
			{
				DataHeader.OriginalSize.LowPart = DataHeader.OriginalSize.HighPart;
			}
			else
			{
				MessageBoxW(NULL, L"Unknown compression method...", 0, 0);
				return STATUS_UNSUCCESSFUL;
			}
		}

		if (GlobalData::GetGlobalData()->DebugOn)
			PrintConsoleW(L"Index Size %08x\n", (ULONG32)DataHeader.OriginalSize.LowPart);

		FormatStringW(IndexFileName, L"%s.index", lpFileName);
		
		LOOP_ONCE
		{
			Status = File.Create(IndexFileName);
			if (NT_FAILED(Status))
				break;

			File.Write(pDecompress, DataHeader.OriginalSize.LowPart);
			File.Close();
		};
	} while (DataHeader.bZlib & 0x80);


	FreeMemoryP(pCompress);
	FreeMemoryP(pDecompress);

	return STATUS_SUCCESS;
}



ForceInline BOOL FASTCALL CheckUnicodeWithoutBOM(const PBYTE pText, LONG length)
{
	int   i;
	DWORD nBytes = 0;
	UCHAR chr;

	BOOL bAllAscii = TRUE;
	for (i = 0; i < length; i++)
	{
		chr = *(pText + i);
		if ((chr & 0x80) != 0)
			bAllAscii = FALSE;
		if (nBytes == 0)
		{
			if (chr >= 0x80)
			{
				if (chr >= 0xFC && chr <= 0xFD)
					nBytes = 6;
				else if (chr >= 0xF8)
					nBytes = 5;
				else if (chr >= 0xF0)
					nBytes = 4;
				else if (chr >= 0xE0)
					nBytes = 3;
				else if (chr >= 0xC0)
					nBytes = 2;
				else
				{
					return FALSE;
				}
				nBytes--;
			}
		}
		else
		{
			if ((chr & 0xC0) != 0x80)
			{
				return FALSE;
			}
			nBytes--;
		}
	}
	if (nBytes > 0)
		return FALSE;

	if (bAllAscii)
		return FALSE;
	
	return TRUE;
}


enum class Encode { ANSI = 1, UNICODE_LE, UNICODE_BE, UTF8, UTF8_NOBOM };

ForceInline Encode FASTCALL DetectEncode(const PBYTE pBuffer, long length)
{
	if (pBuffer[0] == 0xFF && pBuffer[1] == 0xFE)
	{
		return Encode::UNICODE_LE;
	}
	else if (pBuffer[0] == 0xFE && pBuffer[1] == 0xFF)
	{
		return Encode::UNICODE_BE;
	}
	else if (pBuffer[0] == 0xEF && pBuffer[1] == 0xBB && pBuffer[2] == 0xBF)
	{
		return Encode::UTF8;
	}
	else if (CheckUnicodeWithoutBOM(pBuffer, length))
	{
		return Encode::UTF8_NOBOM;
	}
	else
	{
		return Encode::ANSI;
	}
}

///getopt9 lib
ForceInline NTSTATUS FASTCALL ParseCommand(LPCWSTR lpCmd)
{
	NTSTATUS              Status;
	NtFileDisk            File;
	LONG_PTR              Argc;
	PWSTR*                Argv;
	WCHAR                 FileName[MAX_PATH];
	IStream*              Stream;
	tTJSBinaryStream*     BStream;
	STATSTG               Stat;
	NtFileDisk            Writer;
	LARGE_INTEGER         Tranferred, WriteSize, TempSize, Offset;
	ULONG                 ReadSize;
	PBYTE                 Buffer;
	ULONG                 Size;
	
	static BYTE SBuffer[1024 * 64];

	if (lpCmd == NULL || StrLengthW(lpCmd) == 0)
		return STATUS_INVALID_PARAMETER;

	Argv = CmdLineToArgvW((PWSTR)lpCmd, &Argc);
	if (Argv == NULL)
		return STATUS_INVALID_PARAMETER; 

	Status = STATUS_SUCCESS;

	//[+]总之我也没打算加多少个cmd 
	LOOP_ONCE
	{
		//[+] dump index
		//di archive
		if (!StrICompareW(Argv[0], L"di", StrCmp_ToLower))
		{
			if (Argc < 2)
			{
				PrintConsoleA("di command : di archive\n");
				break;
			}

			LOOP_ONCE
			{
				Status = File.Open(Argv[2]);
				if (NT_FAILED(Status))
					break;

				Status = DumpXP3ArchiveIndex(Argv[2], File);
				File.Close();
			};
		}
		//[+] dump file
		//df type[normal/sp] filename
		else if (!StrICompareW(Argv[0], L"df", StrCmp_ToLower))
		{
			PrintConsoleA("warning : dt command only present in krkrz\n");
			if (Argc < 2)
			{
				PrintConsoleA("df command : df filename\n");
				break;
			}
			
			BStream = NULL;
			BStream = CallTVPCreateStream_Debugger(Argv[1]);
			if (BStream == NULL)
			{
				PrintConsoleW(L"Couldn't read file : %s\n", Argv[1]);
				Status = STATUS_NO_MEMORY;
				break;
			}
			
			Stream = ConvertBStreamToIStream_Debugger(BStream);
			if (!Stream)
			{
				PrintConsoleW(L"Couldn't create file : %s\n", Argv[1]);
				Status = STATUS_NO_MEMORY;
				break;
			}

			RtlZeroMemory(FileName, sizeof(FileName));
			StrCopyW(FileName, L"./FileDump");
			if (Nt_GetFileAttributes(FileName) == 0xffffffff)
				CreateDirectoryW(FileName, NULL);

			StrCopyW(FileName, L"/");
			StrCopyW(FileName, Argv[1]);
			
			Status = Writer.Create(FileName);
			if (NT_FAILED(Status))
			{
				PrintConsoleW(L"Couldn't create file for writing : %s\n", Argv[1]);
				Status = STATUS_NO_SUCH_FILE;
				break;
			}

			Stream->Stat(&Stat, STATFLAG_DEFAULT);

			while (Tranferred.QuadPart < (LONG64)Stat.cbSize.QuadPart)
			{
				Stream->Read(SBuffer, sizeof(SBuffer), &ReadSize);
				Tranferred.QuadPart += ReadSize;
				TempSize.QuadPart = 0;
				while (TempSize.QuadPart < ReadSize)
				{
					Writer.Write(SBuffer, ReadSize, &WriteSize);
					TempSize.QuadPart += WriteSize.QuadPart;
				}
			}
			Writer.Close();
		}
		//[+] dump file via list
		else if (!StrICompareW(Argv[0], L"dfl", StrCmp_ToLower))
		{
			PrintConsoleA("warning : dtl command only present in krkrz\n");
			if (Argc < 2)
			{
				PrintConsoleA("df command : dfl filelist\n");
				break;
			}

			Status = File.Open(Argv[1]);
			if (NT_FAILED(Status))
			{
				PrintConsoleW(L"dtl : Couldn't open %s\n", Argv[1]);
				break;
			}

			Size   = File.GetSize32();
			Buffer = (PBYTE)AllocateMemoryP(Size);
			if (Buffer = NULL)
			{
				PrintConsoleW(L"dtl : Insufficient memory\n");
				File.Close();
				break;
			}

			File.Read(Buffer, Size);
			File.Close();

			ml::GrowableArray<ml::String> FileList;

			auto Encode = DetectEncode(Buffer, Size);
			switch (Encode)
			{
				case Encode::ANSI:
				{
					PWSTR UnicodeFile = (PWSTR)AllocateMemoryP(ROUND_UP((Size + 1) * 2, 4), HEAP_ZERO_MEMORY);
					MultiByteToWideChar(932, 0, (PSTR)Buffer, Size, UnicodeFile, ROUND_UP((Size + 1) * 2, 4));
					
					ml::String Holder;
					Holder = UnicodeFile;

					FileList = Holder.SplitLines();
				}
				break;

				case Encode::UNICODE_LE:
				{
					ml::String Holder;
					Holder = (PWSTR)(Buffer + 2);

					FileList = Holder.SplitLines();
				}
				break;

				case Encode::UNICODE_BE:
				{
					for (ULONG i = 0; i < Size / 2 - 1; i++)
					{
						BYTE Char = Buffer[i];
						Buffer[i]   = Buffer[i+1];
						Buffer[i+1] = Char;
					}

					ml::String Holder;
					Holder = (PWSTR)(Buffer + 2);

					FileList = Holder.SplitLines();
				}
				break;

				case Encode::UTF8:
				{
					PWSTR UnicodeFile = (PWSTR)AllocateMemoryP(ROUND_UP((Size + 1) * 2, 4), HEAP_ZERO_MEMORY);
					MultiByteToWideChar(932, 0, (PSTR)Buffer + 3, Size - 3, UnicodeFile, ROUND_UP((Size + 1) * 2, 4));

					ml::String Holder;
					Holder = UnicodeFile;

					FileList = Holder.SplitLines();
				}
				break;

				case Encode::UTF8_NOBOM:
				{
					PWSTR UnicodeFile = (PWSTR)AllocateMemoryP(ROUND_UP((Size + 1) * 2, 4), HEAP_ZERO_MEMORY);
					MultiByteToWideChar(CP_UTF8, 0, (PSTR)Buffer, Size, UnicodeFile, ROUND_UP((Size + 1) * 2, 4));

					ml::String Holder;
					Holder = UnicodeFile;

					FileList = Holder.SplitLines();
				}
				break;
			}
			
			FreeMemoryP(Buffer);
			
			if (FileList.GetSize() == 0)
			{
				PrintConsoleA("Emtry file list\n");
				break;
			}

			RtlZeroMemory(FileName, sizeof(FileName));
			StrCopyW(FileName, L"./FileDump");
			if (Nt_GetFileAttributes(FileName) == 0xffffffff)
				CreateDirectoryW(FileName, NULL);

			for (auto& ItemFileName : FileList)
			{
				RtlZeroMemory(FileName, sizeof(FileName));
				StrCopyW(FileName, L"./FileDump");
				StrCopyW(FileName, L"/");
				
				ULONG Length = StrLengthW(ItemFileName.GetBuffer());
				if (ItemFileName[Length - 1] == L'\r')
				{
					RtlCopyMemory(FileName, ItemFileName.GetBuffer(), (Length - 1) * 2);
				}
				else
				{
					StrCopyW(FileName, ItemFileName.GetBuffer());
				}

				Status = Writer.Create(FileName);
				if (NT_FAILED(Status))
				{
					PrintConsoleW(L"Couldn't create file for writing : %s\n", Argv[1]);
					Status = STATUS_NO_SUCH_FILE;
					break;
				}

				Stream->Stat(&Stat, STATFLAG_DEFAULT);

				while (Tranferred.QuadPart < (LONG64)Stat.cbSize.QuadPart)
				{
					Stream->Read(SBuffer, sizeof(SBuffer), &ReadSize);
					Tranferred.QuadPart += ReadSize;
					TempSize.QuadPart = 0;
					while (TempSize.QuadPart < ReadSize)
					{
						Writer.Write(SBuffer, ReadSize, &WriteSize);
						TempSize.QuadPart += WriteSize.QuadPart;
					}
				}
				Writer.Close();
			}
			
		}
		//???
		else
		{
			PrintConsoleA("Unsupported command : %s\n", Argv[0]);
		}
	};
	ReleaseArgv(Argv);
	return Status;
}

