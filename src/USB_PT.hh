// This Test Port skeleton header file was generated by the
// TTCN-3 Compiler of the TTCN-3 Test Executor version CRL 113 200/6 R5A
// for Harald Welte (laforge@nataraja) on Sun Jan  6 18:08:12 2019

// Copyright (c) 2000-2018 Ericsson Telecom AB

// You may modify this file. Add your attributes and prototypes of your
// member functions here.

#ifndef USB__PT_HH
#define USB__PT_HH

#include <map>
#include <iterator>

#include <TTCN3.hh>
#include <libusb.h>
#include "USB_PortTypes.hh"

// Note: Header file USB_PortType.hh must not be included into this file!
// (because it includes this file)
// Please add the declarations of message types manually.

namespace USB__PortType {

using namespace USB__PortTypes;

class USB_Device;

class USB_Interface {
public:
	USB_Interface(USB_Device &dev, unsigned int number);
	~USB_Interface();
	USB_Device &mDev;
	unsigned int mNum;
};

class USB__PT_PROVIDER;

class USB_Device {
public:
	USB_Device(USB__PT_PROVIDER *pt, libusb_device_handle *handle, unsigned int id);
	USB_Device(const USB_Device &in);
	~USB_Device();
	USB_Interface *interface(unsigned int number);
	void log(TTCN_Logger::Severity msg_severity, const char *fmt, ...);

	/* the TTCN-3 port through which this device was opened */
	USB__PT_PROVIDER *mPort;
	/* the actaul libusb data structure */
	libusb_device_handle *mHandle;
	/* ID the user gave the transfer, returned back to user on completion */
	unsigned int mID;
	/* interfaces we have claimed for this device */
	std::map<unsigned int, USB_Interface *> mInterfaces;
};

class USB_Transfer {
public:
	USB_Transfer(const USB_Device *dev, unsigned int id, struct libusb_transfer *lx);
	USB_Transfer(USB__PT_PROVIDER *pt, const USB__transfer& send_par);
	~USB_Transfer();
	void submit();
	void complete();
	void log(const char *fmt, ...);

	/* the USB device through which the transfer is sent */
	const USB_Device *mDev;
	/* ID the user gave the transfer, returned back to user on completion */
	unsigned int mID;
	/* the actaul libusb data structure */
	struct libusb_transfer *mXfer;
private:
	void init(const USB_Device *dev, unsigned int id, struct libusb_transfer *lx);
};

class USB__PT_PROVIDER : public PORT {
public:
	USB__PT_PROVIDER(const char *par_port_name);
	~USB__PT_PROVIDER();

	void set_parameter(const char *parameter_name,
		const char *parameter_value);

private:
	void Handle_Fd_Event(int fd, boolean is_readable, boolean is_writable, boolean is_error);
	/* void Handle_Timeout(double time_since_last_call); */

protected:
	void user_map(const char *system_port);
	void user_unmap(const char *system_port);

	void user_start();
	void user_stop();

	void outgoing_send(const USB__open__vid__pid& send_par);
	void outgoing_send(const USB__open__path& send_par);
	void outgoing_send(const USB__transfer& send_par);
	void outgoing_send(const USB__set__configuration& send_par);
	void outgoing_send(const USB__claim__interface& send_par);
	void outgoing_send(const USB__release__interface& send_par);
	void outgoing_send(const USB__get__device__descriptor& send_par);
	void outgoing_send(const USB__get__config__descriptor& send_par);
	void outgoing_send(const USB__get__config__descriptor__by__value& send_par);
	void outgoing_send(const USB__get__active__config__descriptor& send_par);
	void outgoing_send(const USB__get__string__descriptor& send_par);

	virtual void incoming_message(const USB__transfer__compl& incoming_par) = 0;
	virtual void incoming_message(const USB__result& incoming_par) = 0;
	virtual void incoming_message(const USB__descriptor& incoming_par) = 0;

public:
	void log(const char *fmt, ...);
	USB_Device *usbdev_by_hdl(unsigned int hdl);
	void transfer_completed(USB_Transfer *t);
private:
	libusb_context *mCtx;
	std::map<unsigned int, USB_Device *> mDevices;

	unsigned int usbhdl_by_dev(libusb_device_handle *dh);

	/* to make them access protected Handler_{Add,Remove}_Fd methods */
	friend void libusb_pollfd_added(int fd, short events, void *user_data);
	friend void libusb_pollfd_removed(int fd, void *user_data);
	friend void libusb_transfer_cb(struct libusb_transfer *xfer);
};

} /* end of namespace */

#endif
