/*
 * UsbCdcIoChannel.cpp
 *
 * IOChannel via CDC (VCOM) over USB communication.
 *
 * Copyright (C) 2007 - 2011 Texas Instruments Incorporated - http://www.ti.com/ 
 * 
 * 
 *  Redistribution and use in source and binary forms, with or without 
 *  modification, are permitted provided that the following conditions 
 *  are met:
 *
 *    Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer.
 *
 *    Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the   
 *    distribution.
 *
 *    Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                                                                                                                                                                                                                                                                                                         
 * OSX implemenation was derived from the qextserialport project - https://code.google.com/p/qextserialport/
 * The qextserialport is under MIT 
 */

#ifdef _MSC_VER
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS //disabling warnings to use secure c-function versions (e.g. strcpy_s) as this is not compatible with none MS development
#endif
#endif

#include <fstream>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/thread/thread.hpp>

#include "UsbCdcIoChannel.h"
#include "logging/Logging.h"

#if defined(_WIN32) || defined(_WIN64)

extern "C" {
	#include <setupapi.h>
	#include <dbt.h>
}

#else

	#include <unistd.h>
	#include <boost/filesystem.hpp>

	using namespace boost::filesystem;

#endif

#ifdef  __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/IOMessage.h>
#include <mach/mach_port.h>
#include <IOKit/serial/IOSerialKeys.h> 
#define MAXPATHLEN 128
#define MAXNAMELEN 64

struct CDCPortInfo {
	char *portName;
	char *physName;
	char *friendName;
	int vendorID;
	int productID;
};
#endif

using namespace TI::DLL430;
using namespace std;
using namespace boost::asio;

#define  XOFF		0x13
#define  XON		0x11
#define  XMASK		0x10
#define  XMASK_OFF	0x03
#define  XMASK_ON	0x01
#define  XMASK_MASK	0x00

#ifndef NDEBUG
#define DB_PRINT
#endif

UsbCdcIoChannel::UsbCdcIoChannel(const PortInfo& portInfo)
 : UsbIoChannel(portInfo)
 , outputReportSize(255)
 , inputReportSize(255)
 , actSize(0)
 , expSize(0)
 , ioService(0)
 , port(0)
 , comState(ComStateRcv)
{
	retrieveStatus();
}

UsbCdcIoChannel::~UsbCdcIoChannel()
{
	this->cleanup();
}

void UsbCdcIoChannel::createCcdPortList(const std::string& cdcId, PortMap& portList)
{
#if defined(_WIN32) || defined(_WIN64)
	const int BUFFER_SIZE = 128;

	HDEVINFO hDevInfo = ::SetupDiGetClassDevs(NULL, "USB", 0, DIGCF_PRESENT | DIGCF_ALLCLASSES );
	SP_DEVINFO_DATA devInfoData;
	devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
	
	for (int i = 0; ::SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); ++i )
	{	
		char deviceId[BUFFER_SIZE] = {0};
		BOOL result = ::SetupDiGetDeviceInstanceId(hDevInfo, &devInfoData, deviceId, BUFFER_SIZE, NULL);

		//not TI and/or not CDC
		if( result && string(deviceId).find(cdcId) != string::npos)
		{
			DWORD propertyType = 0;
			BYTE property[BUFFER_SIZE] = {0};

			::SetupDiGetDeviceRegistryProperty(hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME, &propertyType, property, BUFFER_SIZE, NULL);

			stringstream sstr;
			for (int k = 0; k < BUFFER_SIZE && property[k] != 0; ++k)
			{
				sstr << property[k];
			}

			const size_t idBegin = sstr.str().find_last_of('(') + 1;
			const size_t idEnd = sstr.str().find_last_of(')');
			assert(idEnd > idBegin);

			const string name = sstr.str().substr(idBegin, idEnd - idBegin);
	
			if((name[0] && (sstr.str().compare(0,19,"MSP Debug Interface") == 0 ))|| (name[0] && (sstr.str().compare(0,19,"MSP-FET430UIF - CDC") == 0 ))) 
            {
			    PortInfo portInfo(name, string("\\\\.\\")+name, PortInfo::CDC, retrieveSerialFromId(deviceId));
				if(name[0] && (sstr.str().compare(0,19,"MSP Debug Interface") == 0 ))
				{
					portInfo.useFlowControl = false;
					portInfo.useCrc = false;
				}
				else if(name[0] && (sstr.str().compare(0,19,"MSP-FET430UIF - CDC") == 0 ))
				{
					portInfo.useFlowControl = true;
					portInfo.useCrc = true;
				}
			    
				//if (open)
				{
					portInfo.status = UsbCdcIoChannel(portInfo).getStatus();
				}
				portList[portInfo.name] = portInfo;
            }
		}
	}
	::SetupDiDestroyDeviceInfoList(hDevInfo);//free resources

#endif
#ifdef __linux__
	path p("/sys/class/tty/");
	if (exists(p) && is_directory(p))
	{
		const directory_iterator end;
		for (directory_iterator it(p); it != end; ++it)
		{
			string dir = it->path().c_str();
			if (dir.find("ttyACM") != string::npos)
			{
				string modalias;
				int interfaceNumber = -1;
				
				ifstream modAliasStream((it->path()/"device/modalias").c_str());
				modAliasStream >> modalias;
				
				ifstream ifNumStream((it->path()/"device/bInterfaceNumber").c_str());
				ifNumStream >> interfaceNumber;
				if (modalias.find(cdcId) == 0 && interfaceNumber == 0)
				{
					const string filename = it->path().filename().c_str();
					const string portPath = string("/dev/") + filename;

					PortInfo portInfo(filename, portPath, PortInfo::CDC);
					
					if (cdcId == "usb:v2047p0013")
					{
						portInfo.useFlowControl = false;
						portInfo.useCrc = false;
					}
					else if (cdcId == "usb:v2047p0010")
					{
						portInfo.useFlowControl = true;
						portInfo.useCrc = true;
					}

					//if (open)
					{
						portInfo.status = UsbCdcIoChannel(portInfo).getStatus();
					}
					portList[portInfo.name] = portInfo;
				}
			}
		}
	}
#endif
#ifdef __APPLE__

	CFMutableDictionaryRef matchingDict;
	kern_return_t kernResult;
	io_iterator_t iterator;

	matchingDict = IOServiceMatching(kIOSerialBSDServiceValue);
	CFDictionarySetValue(matchingDict, CFSTR(kIOSerialBSDTypeKey), CFSTR(kIOSerialBSDModemType));
	kernResult = IOServiceGetMatchingServices(kIOMasterPortDefault, matchingDict, &iterator);

	if (kernResult != KERN_SUCCESS) {
		return;
	}

	for (;;) {
		if (!IOIteratorIsValid(iterator)) {
			/*
			* Apple documentation advises resetting the iterator if
			* it should become invalid during iteration.
			*/
			IOIteratorReset(iterator);
			continue;
		}

		io_service_t device = IOIteratorNext(iterator);

		if (device == 0) {
			break;
		}

		struct CDCPortInfo info = {0, 0, NULL, NULL, NULL};

		bool retval = true;
		CFTypeRef bsdPathAsCFString = NULL;
		CFTypeRef productNameAsCFString = NULL;
		CFTypeRef vendorIdAsCFNumber = NULL;
		CFTypeRef productIdAsCFNumber = NULL;
		CFTypeRef ttyDeviceAsCFString = NULL;
		CFTypeRef interfaceNumberAsCFNumber = NULL;

		// Get the name of the modem's callout device
		bsdPathAsCFString = IORegistryEntryCreateCFProperty(device, CFSTR(kIOCalloutDeviceKey),
									kCFAllocatorDefault, 0);

		ttyDeviceAsCFString = IORegistryEntryCreateCFProperty(device, CFSTR(kIOTTYDeviceKey),
									 kCFAllocatorDefault, 0);

		io_name_t name;
		IORegistryEntryGetName(device, name);

		// wander up the hierarchy until we find the level that can give us the
		// vendor/product IDs and the product name, if available
		io_registry_entry_t parent;
		kern_return_t kernResult = IORegistryEntryGetParentEntry(device, kIOServicePlane, &parent);
		IOObjectRelease(device);

		while( kernResult == KERN_SUCCESS && !vendorIdAsCFNumber && !productIdAsCFNumber )
		{
			if(!productNameAsCFString)
				productNameAsCFString = IORegistryEntrySearchCFProperty(parent,
										kIOServicePlane,
										CFSTR("Product Name"),
										kCFAllocatorDefault, 0);

			if(!vendorIdAsCFNumber)
				vendorIdAsCFNumber = IORegistryEntrySearchCFProperty(parent,
									kIOServicePlane,
									CFSTR(kUSBVendorID),
									kCFAllocatorDefault, 0);
			if(!productIdAsCFNumber)
				productIdAsCFNumber = IORegistryEntrySearchCFProperty(parent,
									kIOServicePlane,
									CFSTR(kUSBProductID),
									kCFAllocatorDefault, 0);
			if(interfaceNumberAsCFNumber)
				interfaceNumberAsCFNumber = IORegistryEntrySearchCFProperty(parent,
									kIOServicePlane,
									CFSTR(kUSBInterfaceNumber),
									kCFAllocatorDefault, 0);

			io_registry_entry_t oldparent = parent;
			kernResult = IORegistryEntryGetParentEntry(parent, kIOServicePlane, &parent);
			IOObjectRelease(oldparent);
		}

		if(interfaceNumberAsCFNumber)
		{
			SInt32 interfaceNumber;
			if(CFNumberGetValue((CFNumberRef)interfaceNumberAsCFNumber, kCFNumberSInt32Type, &interfaceNumber))
				CFRelease(interfaceNumberAsCFNumber);
			if(interfaceNumber != 0x01)
				continue;
		}

		if(ttyDeviceAsCFString) {
			char ttyDevice[MAXNAMELEN];
			if( CFStringGetCString((CFStringRef)ttyDeviceAsCFString, ttyDevice,
						PATH_MAX, kCFStringEncodingUTF8) ) {
				info.portName = ttyDevice;
			}
			CFRelease(ttyDeviceAsCFString);
		}

		if(bsdPathAsCFString) {
			char path[MAXPATHLEN];
			if(CFStringGetCString((CFStringRef)bsdPathAsCFString, path,
						PATH_MAX, kCFStringEncodingUTF8)) {
				info.physName = path;
			}
			CFRelease(bsdPathAsCFString);
		}

		if(productNameAsCFString) {
			char productName[MAXPATHLEN];
			if(CFStringGetCString((CFStringRef)productNameAsCFString, productName,
						PATH_MAX, kCFStringEncodingUTF8)) {
				info.friendName = productName;
			}
			CFRelease(productNameAsCFString);
		}

		if(vendorIdAsCFNumber) {
			SInt32 vID;
			if(CFNumberGetValue((CFNumberRef)vendorIdAsCFNumber, kCFNumberSInt32Type, &vID)) {
				info.vendorID = vID;
			}
			CFRelease(vendorIdAsCFNumber);
		}

		if(productIdAsCFNumber) {
			SInt32 pID;
			if(CFNumberGetValue((CFNumberRef)productIdAsCFNumber, kCFNumberSInt32Type, &pID)) {
				info.productID = pID;
			}
			CFRelease(productIdAsCFNumber);
		}

		if(info.vendorID == 0x2047 && info.productID == 0x13) {
			PortInfo portInfo(info.portName, info.physName, PortInfo::CDC);
			portInfo.useFlowControl = false;
			portInfo.useCrc = false;
			portInfo.status = UsbCdcIoChannel(portInfo).getStatus();
			portList[portInfo.name] = portInfo;
#if 0
			printf("Name: %s\n", info.friendName);
			printf("\tport name: %s\n", info.portName);
			printf("\tvid: 0x%02x\n", info.vendorID);
			printf("\tpid: 0x%02x\n", info.productID);
			printf("\tpath: %s\n", info.physName);
#endif
		}
	}
#endif
}


void UsbCdcIoChannel::enumeratePorts (PortMap& portList, bool open)
{
#if defined(_WIN32) || defined(_WIN64)	
	const std::string eZ_FET_CdcId = "USB\\VID_2047&PID_0013";
	const std::string MSP_FET_430_CdcId = "USB\\VID_2047&PID_0010";
#else
	const std::string eZ_FET_CdcId = "usb:v2047p0013";
	const std::string MSP_FET_430_CdcId = "usb:v2047p0010";
#endif	

	createCcdPortList(eZ_FET_CdcId, portList);
	createCcdPortList(MSP_FET_430_CdcId, portList);
}

std::string UsbCdcIoChannel::retrieveSerialFromId(const std::string& id)
{
#if defined(_WIN32) || defined(_WIN64)
	const size_t idBegin = id.find_last_of('\\') + 1;
	return id.substr(idBegin, 16);
#else
	const size_t begin = id.find_last_of('_') + 1;
	const size_t end = id.find_last_of('-');
	return id.substr( begin, end - begin );
#endif
}

bool UsbCdcIoChannel::openPort()
{
	ioService = new boost::asio::io_service;
	port = new boost::asio::serial_port(*ioService);

	if ( boost::system::error_code ec = port->open(portInfo.path, ec) )
	{
		int retry = 5;
		while (ec && --retry )
		{
			boost::this_thread::sleep(boost::get_system_time() + boost::posix_time::milliseconds(5));
			ec = port->open(portInfo.path, ec);
		}

		if ( ec == boost::system::error_condition(boost::system::errc::permission_denied) )
			portInfo.status = PortInfo::inUseByAnotherInstance;
		if (ec)
		{
			close();
			return false;
		}
	}
	return true;
}

void UsbCdcIoChannel::retrieveStatus()
{
	portInfo.status = PortInfo::freeForUse;

	if (!isOpen())
	{
		openPort();
		//Seeing issues on some platforms (eg. Ubuntu) when port is immediately closed again
		boost::this_thread::sleep(boost::get_system_time() + boost::posix_time::milliseconds(100));
		close();
	}
}


bool UsbCdcIoChannel::open()
{
	if (!isOpen() && !openPort())
	{
		return false;
	}

	portInfo.status = PortInfo::freeForUse;

	try
	{
		const int baudrate = 460800;

		port->set_option( serial_port::baud_rate( baudrate ) );
		port->set_option( serial_port::flow_control( serial_port::flow_control::none ) );
		port->set_option( serial_port::parity( serial_port::parity::none ) );
		port->set_option( serial_port::stop_bits( serial_port::stop_bits::one ) );
		port->set_option( serial_port::character_size(8) );
	}
	catch (const boost::system::system_error&) {}

	inputBuffer.resize(inputReportSize+3);

	return true;
}

void UsbCdcIoChannel::cleanup() 
{
	if (isOpen())
	{
		boost::system::error_code ec = port->close(ec);
	}
	delete port;
	port = 0;
	delete ioService;
	ioService = 0;
}

bool UsbCdcIoChannel::close() 
{
	cleanup();
	return true;
}

bool UsbCdcIoChannel::isOpen() const
{
	return port && port->is_open();
}

boost::mutex readWriteMutex;

class AsyncTransferHandler
{
public:	
	AsyncTransferHandler(serial_port& port) : 
		port(port), 
		timer(port.get_io_service()), 
		result(RES_NONE), 
		bytesTransfered(0) {}

	int read(unsigned char* buf, size_t bufSize, uint32_t timeout)
	{
		timer.expires_from_now(boost::posix_time::milliseconds(timeout));
		timer.async_wait(boost::bind(&AsyncTransferHandler::onTimeout, this, _1));

		async_read(port, buffer(buf, bufSize),
				 boost::bind(&AsyncTransferHandler::onTransferComplete, this, _1, _2) );

		port.get_io_service().reset();

#if defined(_WIN32) || defined(_WIN64)
		while ( result == RES_NONE )
			port.get_io_service().run_one();

		if (result != RES_COMPLETE)
		{
			readWriteMutex.lock();
			port.cancel();
			readWriteMutex.unlock();
		}

		if (result != RES_TIMEOUT)
			timer.cancel();
#else
		while ( result == RES_NONE )
			port.get_io_service().run();
#endif
		port.get_io_service().stop();

		return (result == RES_COMPLETE) ? static_cast<int>(bytesTransfered) : -1;
	}	

private:
	enum Result { RES_NONE, RES_ERROR, RES_TIMEOUT, RES_COMPLETE };

	serial_port& port;
	deadline_timer timer;
	Result result;
	int bytesTransfered;

	void onTransferComplete(const boost::system::error_code& ec, size_t numBytes)
	{
#ifdef UNIX
		timer.cancel();
#endif
		if ( ec != error::operation_aborted && result == RES_NONE )
			result = ec ? RES_ERROR : RES_COMPLETE;

		bytesTransfered = (result == RES_COMPLETE) ? static_cast<int>(numBytes) : -1;
	}

	void onTimeout(const boost::system::error_code& ec)
	{
#ifdef UNIX
		port.cancel();
#endif
		if( ec != error::operation_aborted && result == RES_NONE )
			result = ec ? RES_ERROR : RES_TIMEOUT;
	}
};

int UsbCdcIoChannel::read(HalResponse& resp, uint32_t timeout)
{
	if (!isOpen())
		return 0;

	if(actSize == 0)
		expSize = 6;

	if (expSize > inputBuffer.size())
		inputBuffer.resize(expSize);

	int bytesRead = -1;
	try
	{
		AsyncTransferHandler readHandler(*port);
		bytesRead = readHandler.read(&inputBuffer[actSize], expSize-actSize, timeout);
	} catch(const std::exception&) {}

	if (bytesRead < 0)
	{
		//Important, test port must be gone before close can be called
		boost::system::error_code ec = serial_port(*ioService).open(portInfo.path, ec);

		if (ec == boost::system::error_condition(boost::system::errc::no_such_file_or_directory))
		{
			if (comState != ComStateDisconnect)
			{
				this->close();
			}
			comState = ComStateDisconnect;
		}
	}

	if (bytesRead > 0)
	{
		if (actSize == 0)
			expSize = inputBuffer[0] + ( (inputBuffer[0] & 0x01) ? 3 : 4);

		actSize += static_cast<uint16_t>(bytesRead & 0xFFFF);

		//Perform sanity check on message size before trying to read it
		if (expSize < actSize || (expSize % 2) != 0)
		{
			resp.setError(HalResponse::Error_Size);
			expSize = actSize = 0;
			return bytesRead;
		}


		if(expSize && actSize == expSize)
		{
	#ifdef DB_PRINT
			Logging::DefaultLogger().PrintReceiveBuffer(&inputBuffer[0], expSize);
	#endif // DB_PRINT

			const uint16_t msgSize = expSize;
			actSize=0;
			expSize=0;

			resp.setType(inputBuffer[1]);
			resp.setId(inputBuffer[2] & 0x7f); //Don't mask async bit (0x40)
			resp.setIsComplete(inputBuffer[2]);
			
			if(msgSize >= 2)
			{
				resp.append(&inputBuffer[1], inputBuffer[0]);
			}

			return bytesRead;
		}
	}
    return 0;
}


enum ComState UsbCdcIoChannel::poll()
{
	return comState;
}


int UsbCdcIoChannel::write(const uint8_t* payload, size_t len)
{
	if (!isOpen())
		return 0;

	int ret_len = static_cast<int>(len);

	uint8_t report[256] = {0};

	if (payload)
		memcpy(report, payload, len);

	// test for fill byte
	if (!(report[0] & 0x01))
		report[len++] = 0x00;

	if (portInfo.useCrc)
	{
		//create crc and append it to data
		uint16_t crc = createCrc(report);

		report[len++] = crc & 0x00ff;
		report[len++] = (crc & 0xff00) >> 8;
	}

	size_t n_write = 0;
	uint8_t send_buf[512];

	if (portInfo.useFlowControl)
	{
		//mask XOFF, XON and MASK in data stream
		size_t j = 0;
		
		for (size_t i = 0; i < len; i++)
		{
			const uint8_t ch = report[i];
			switch (ch)
			{
			case XOFF:
			case XON:
			case XMASK:
				send_buf[j] = XMASK;
				j++;
				send_buf[j] = ch & 0x3;
				break;
			default:
				send_buf[j] = ch;
			}
			j++;
		}
		n_write = j;
	}
	else
	{
		n_write = len;
		memcpy(send_buf, report, n_write);
	}

	readWriteMutex.lock();
	boost::system::error_code ec;
	const size_t nWritten = boost::asio::write(*port, buffer(send_buf, n_write), transfer_all(), ec);
	readWriteMutex.unlock();
	if (nWritten != n_write)
	{
		return 0;
	}

#ifdef DB_PRINT
	Logging::DefaultLogger().PrintSendBuffer(send_buf, n_write);
#endif // DB_PRINT
	return ret_len;
}

size_t UsbCdcIoChannel::getPayloadMaxLength() const
{
	return (this->outputReportSize - 3);
}

const char* UsbCdcIoChannel::getName() const
{
	return portInfo.name.c_str();
}

string UsbCdcIoChannel::getSerial() const
{
	return portInfo.serial;
}

PortInfo::Status UsbCdcIoChannel::getStatus() const
{
 	return portInfo.status;
}
