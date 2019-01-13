/* Copyright (c) 2018 Harald Welte <laforge@gnumonks.org> */

#include "USB_PT.hh"
#include "USB_PortType.hh"

#include <poll.h>
#include <libusb.h>

using namespace USB__PortTypes;

namespace USB__PortType {

/***********************************************************************
 * libusb callbacks / helpers
 ***********************************************************************/

static enum libusb_transfer_type ttype_titan2usb(const USB__transfer__type &in)
{
	switch (in) {
	case USB__transfer__type::USB__TRANSFER__TYPE__CONTROL:
		return LIBUSB_TRANSFER_TYPE_CONTROL;
	case USB__transfer__type::USB__TRANSFER__TYPE__ISOCHRONOUS:
		return LIBUSB_TRANSFER_TYPE_ISOCHRONOUS;
	case USB__transfer__type::USB__TRANSFER__TYPE__BULK:
		return LIBUSB_TRANSFER_TYPE_BULK;
	case USB__transfer__type::USB__TRANSFER__TYPE__INTERRUPT:
		return LIBUSB_TRANSFER_TYPE_INTERRUPT;
	case USB__transfer__type::USB__TRANSFER__TYPE__BULK__STREAM:
		return LIBUSB_TRANSFER_TYPE_BULK_STREAM;
	default:
		TTCN_error("Unknown USB transfer type %d", (int)in);
	}
}

static USB__transfer__type ttype_usb2titan(enum libusb_transfer_type in)
{
	switch (in) {
	case LIBUSB_TRANSFER_TYPE_CONTROL:
		return USB__transfer__type::USB__TRANSFER__TYPE__CONTROL;
	case LIBUSB_TRANSFER_TYPE_ISOCHRONOUS:
		return USB__transfer__type::USB__TRANSFER__TYPE__ISOCHRONOUS;
	case LIBUSB_TRANSFER_TYPE_BULK:
		return USB__transfer__type::USB__TRANSFER__TYPE__BULK;
	case LIBUSB_TRANSFER_TYPE_INTERRUPT:
		return USB__transfer__type::USB__TRANSFER__TYPE__INTERRUPT;
	case LIBUSB_TRANSFER_TYPE_BULK_STREAM:
		return USB__transfer__type::USB__TRANSFER__TYPE__BULK__STREAM;
	default:
		TTCN_error("Unknown USB transfer type %d", (int)in);
	}
}

/* call-back by libusb telling us to monitor a given fd */
void libusb_pollfd_added(int fd, short events, void *user_data)
{
	TTCN_warning("pollfd_added(%d, %d)", fd, events);
	USB__PT_PROVIDER *pt = (USB__PT_PROVIDER *)user_data;
	if (events & POLLIN)
		pt->Handler_Add_Fd_Read(fd);
	if (events & POLLOUT)
		pt->Handler_Add_Fd_Write(fd);
}

/* call-back by libusb telling us to stop monitoring a given fd */
void libusb_pollfd_removed(int fd, void *user_data)
{
	TTCN_warning("pollfd_removed(%d)", fd);
	USB__PT_PROVIDER *pt = (USB__PT_PROVIDER *)user_data;
	pt->Handler_Remove_Fd(fd);
}

void libusb_transfer_cb(struct libusb_transfer *xfer)
{
	USB_Transfer *t = (USB_Transfer *) xfer->user_data;
	t->complete();
	delete t;
}


/***********************************************************************
 * USB_Interface
 ***********************************************************************/

USB_Interface::USB_Interface(USB_Device &dev, unsigned int number)
	: mDev(dev), mNum(number)
{
	mDev.log(TTCN_WARNING, "creating USB interface %u\n", mNum);
}


USB_Interface::~USB_Interface()
{
	mDev.log(TTCN_WARNING, "releasing USB interface %u\n", mNum);
	int rc = libusb_release_interface(mDev.mHandle, mNum);
	if (rc != 0)
		mDev.log(TTCN_WARNING, "cannot release interface");
}
/***********************************************************************
 * USB_Device
 ***********************************************************************/

USB_Device::USB_Device(USB__PT_PROVIDER *port, libusb_device_handle *handle, unsigned int id)
	: mPort(port), mHandle(handle), mID(id)
{
	log(TTCN_WARNING, "creating");
}

USB_Device::USB_Device(const USB_Device &in)
{
	this->mPort = in.mPort;
	this->mHandle = in.mHandle;
	this->mID = in.mID;
	log(TTCN_WARNING, "copying from %p", &in);
}

USB_Device::~USB_Device()
{
	log(TTCN_WARNING, "destroying");

	/* iterate over map of interfaces: destroy the USB_Interface objects in it; clear map */
	std::map<unsigned int,USB_Interface *>::iterator it;
	for (it = mInterfaces.begin(); it != mInterfaces.end(); it++) {
		delete it->second;
	}
	mInterfaces.clear();

	libusb_close(mHandle);
}

USB_Interface *USB_Device::interface(unsigned int number)
{
	std::map<unsigned int,USB_Interface *>::iterator it = mInterfaces.find(number);
	if (it == mInterfaces.end())
		TTCN_error("Cannot find USB interface %u", number);
	return it->second;
}

void USB_Device::log(TTCN_Logger::Severity msg_severity, const char *fmt, ...)
{
	TTCN_Logger::begin_event(msg_severity);
	TTCN_Logger::log_event("USB Test port (%s): Device %p(%p,%d) ", mPort->get_name(), this, mHandle, mID);
	va_list args;
	va_start(args, fmt);
	TTCN_Logger::log_event_va_list(fmt, args);
	va_end(args);
	TTCN_Logger::end_event();
}




/***********************************************************************
 * USB_Transfer
 ***********************************************************************/

USB_Transfer::USB_Transfer(const USB_Device *dev, unsigned int id, struct libusb_transfer *lx)
{
	init(dev, id, lx);
}

USB_Transfer::USB_Transfer(USB__PT_PROVIDER *pt, const USB__transfer& send_par)
{
	USB_Device *dev = pt->usbdev_by_hdl(send_par.device__hdl());

	struct libusb_transfer *xfer = libusb_alloc_transfer(0);
	xfer->dev_handle = dev->mHandle;
	xfer->flags = 0;
	xfer->type = ttype_titan2usb(send_par.ttype());
	xfer->endpoint = send_par.endpoint();
	xfer->timeout = send_par.timeout__msec();
	xfer->user_data = this;
	xfer->length = send_par.data().lengthof();

	/* Control IN/read transfer */
	if (xfer->type == LIBUSB_TRANSFER_TYPE_CONTROL && send_par.data()[0].get_octet() & 0x80) {
		xfer->length += (send_par.data()[7].get_octet() << 8) + send_par.data()[6].get_octet();
	}
	xfer->buffer = libusb_dev_mem_alloc(xfer->dev_handle, xfer->length);
	memcpy(xfer->buffer, send_par.data(), xfer->length);
	xfer->callback = &libusb_transfer_cb;
	init(dev, send_par.transfer__hdl(), xfer);
};

void USB_Transfer::init(const USB_Device *dev, unsigned int id, struct libusb_transfer *lx)
{
	mDev = dev;
	mID = id;
	mXfer = lx;
	log("transfer created\n");
}

void USB_Transfer::submit()
{
	int rc;

	rc = libusb_submit_transfer(mXfer);
	if (rc != 0) {
		TTCN_error("Error during libusb_submit_transfer(): %s",
			   libusb_strerror((libusb_error) rc));
	}
	log("transfer submitted\n");
}

void USB_Transfer::complete()
{
	log("transfer completed\n");
	/* report back to TTCN-3 port user */
	mDev->mPort->transfer_completed(this);
}

void USB_Transfer::log(const char *fmt, ...)
{
	TTCN_Logger::begin_event(TTCN_WARNING);
	TTCN_Logger::log_event("USB Test port (%s): T %p ", mDev->mPort->get_name(), this);
	va_list args;
	va_start(args, fmt);
	TTCN_Logger::log_event_va_list(fmt, args);
	va_end(args);
	TTCN_Logger::end_event();
}


USB_Transfer::~USB_Transfer()
{
	/* last, but not least: release the buffer used */
	if (mXfer->buffer) {
		libusb_dev_mem_free(mDev->mHandle, mXfer->buffer, mXfer->length);
		mXfer->buffer = NULL;
	}
	libusb_free_transfer(mXfer);
	log("transfer destroyed\n");
}


/***********************************************************************
 * USB__PT_PROVIDER
 ***********************************************************************/

USB__PT_PROVIDER::USB__PT_PROVIDER(const char *par_port_name)
	: PORT(par_port_name)
{
	libusb_init(&mCtx);
	libusb_set_pollfd_notifiers(mCtx, libusb_pollfd_added, libusb_pollfd_removed, this);
}

USB__PT_PROVIDER::~USB__PT_PROVIDER()
{
}

void USB__PT_PROVIDER::log(const char *fmt, ...)
{
	TTCN_Logger::begin_event(TTCN_WARNING);
	TTCN_Logger::log_event("USB Test port (%s): ", get_name());
	va_list args;
	va_start(args, fmt);
	TTCN_Logger::log_event_va_list(fmt, args);
	va_end(args);
	TTCN_Logger::end_event();
}

void USB__PT_PROVIDER::set_parameter(const char *parameter_name, const char *parameter_value)
{
	if (0) {
		/* handle known/supported parameters here */
	} else
		TTCN_error("Unsupported test port parameter `%s'.", parameter_name);
}

void USB__PT_PROVIDER::Handle_Fd_Event(int fd, boolean is_readable,
	boolean is_writable, boolean is_error)
{
	/* we assume that we're running Linux v2.6.27 with timerfd support here
	 * and hence don't have to perform manual timeout handling.  See
	 * "Notes on time-based events" at
	 * http://libusb.sourceforge.net/api-1.0/group__libusb__poll.html */
	struct timeval zero_tv = { 0, 0 };
	libusb_handle_events_timeout(mCtx, &zero_tv);
}

/*void USB__PT_PROVIDER::Handle_Timeout(double time_since_last_call) {}*/

void USB__PT_PROVIDER::user_map(const char * /*system_port*/)
{

}

void USB__PT_PROVIDER::user_unmap(const char * /*system_port*/)
{
	/* iterate over map of devices: destroy the USB_Device objects in it; clear map */
	std::map<unsigned int,USB_Device *>::iterator it;
	for (it = mDevices.begin(); it != mDevices.end(); it++) {
		delete it->second;
	}
	mDevices.clear();
}

void USB__PT_PROVIDER::user_start()
{
}

void USB__PT_PROVIDER::user_stop()
{
}

USB_Device *USB__PT_PROVIDER::usbdev_by_hdl(unsigned int hdl)
{
	std::map<unsigned int,USB_Device *>::iterator search = mDevices.find(hdl);
	if (search == mDevices.end()) {
		TTCN_error("Cannot find USB device for handle %u\n", hdl);
	}
	return search->second;
}

unsigned int USB__PT_PROVIDER::usbhdl_by_dev(libusb_device_handle *dh)
{
	std::map<unsigned int,USB_Device *>::iterator it = mDevices.begin();
	while (it != mDevices.end()) {
		if (it->second->mHandle == dh)
			return it->first;
	}

	TTCN_error("Cannot find USB handle for lubusb_dev %p\n", dh);
}


void USB__PT_PROVIDER::outgoing_send(const USB__open__vid__pid& send_par)
{
	unsigned int device_hdl = send_par.device__hdl();
	int rc;

	libusb_device_handle *dh;
	dh = libusb_open_device_with_vid_pid(mCtx, send_par.vendor__id(), send_par.product__id());
	if (!dh) {
		log("Error opening VID/PID %04x:%04x", send_par.vendor__id(), send_par.product__id());
		rc = -1;
	} else {
		USB_Device *dev = new USB_Device(this, dh, device_hdl);
		mDevices.insert(std::make_pair(device_hdl, dev));
		rc = 0;
	}
	incoming_message(USB__result(send_par.req__hdl(), send_par.device__hdl(), rc));
}

void USB__PT_PROVIDER::outgoing_send(const USB__set__configuration& send_par)
{
	USB_Device *dev = usbdev_by_hdl(send_par.device__hdl());
	int rc;

	rc = libusb_set_configuration(dev->mHandle, send_par.configuration());
	if (rc != 0)
		log("Cannot set configuration %d", (int)send_par.configuration());
	incoming_message(USB__result(send_par.req__hdl(), send_par.device__hdl(), rc));
}


void USB__PT_PROVIDER::outgoing_send(const USB__claim__interface& send_par)
{
	USB_Device *dev = usbdev_by_hdl(send_par.device__hdl());
	unsigned int interface_nr = send_par.interface();
	int rc;

	rc = libusb_claim_interface(dev->mHandle, interface_nr);
	if (rc != 0)
		log("Cannot claim USB interface %d\n", interface_nr);
	else {
		USB_Interface *intf = new USB_Interface(*dev, interface_nr);
		dev->mInterfaces.insert(std::make_pair(interface_nr, intf));
	}
	incoming_message(USB__result(send_par.req__hdl(), send_par.device__hdl(), rc));
}

void USB__PT_PROVIDER::outgoing_send(const USB__release__interface& send_par)
{
	USB_Device *dev = usbdev_by_hdl(send_par.device__hdl());
	unsigned int interface_nr = send_par.interface();
	int rc;

	rc = libusb_release_interface(dev->mHandle, interface_nr);
	if (rc != 0)
		log("Cannot release USB interface %d\n", interface_nr);
	else
		dev->mInterfaces.erase(interface_nr);
	incoming_message(USB__result(send_par.req__hdl(), send_par.device__hdl(), rc));
}

void USB__PT_PROVIDER::outgoing_send(const USB__get__device__descriptor& send_par)
{
	USB_Device *dev = usbdev_by_hdl(send_par.device__hdl());
	struct libusb_device_descriptor udesc;
	struct USB__descriptor resp;
	OCTETSTRING data;
	int rc;

	struct libusb_device *d = libusb_get_device(dev->mHandle);
	rc = libusb_get_device_descriptor(d, &udesc);
	if (rc != 0)
		data = OCTETSTRING();
	else
		data = OCTETSTRING(sizeof(udesc), (const unsigned char *) &udesc);
	incoming_message(USB__descriptor(send_par.req__hdl(), send_par.device__hdl(), rc, data));
}

void USB__PT_PROVIDER::outgoing_send(const USB__get__config__descriptor& send_par)
{
	USB_Device *dev = usbdev_by_hdl(send_par.device__hdl());
	struct libusb_config_descriptor *udesc;
	struct USB__descriptor resp;
	OCTETSTRING data;
	int rc;

	struct libusb_device *d = libusb_get_device(dev->mHandle);
	rc = libusb_get_config_descriptor(d, send_par.config__index(), &udesc);
	if (rc != 0)
		data = OCTETSTRING();
	else {
		data = OCTETSTRING(udesc->wTotalLength, (const unsigned char *) udesc);
		libusb_free_config_descriptor(udesc);
	}
	incoming_message(USB__descriptor(send_par.req__hdl(), send_par.device__hdl(), rc, data));
}

void USB__PT_PROVIDER::outgoing_send(const USB__get__config__descriptor__by__value& send_par)
{
	USB_Device *dev = usbdev_by_hdl(send_par.device__hdl());
	struct libusb_config_descriptor *udesc;
	struct USB__descriptor resp;
	OCTETSTRING data;
	int rc;

	struct libusb_device *d = libusb_get_device(dev->mHandle);
	rc = libusb_get_config_descriptor_by_value(d, send_par.config__value(), &udesc);
	if (rc != 0)
		data = OCTETSTRING();
	else {
		data = OCTETSTRING(udesc->wTotalLength, (const unsigned char *) udesc);
		libusb_free_config_descriptor(udesc);
	}
	incoming_message(USB__descriptor(send_par.req__hdl(), send_par.device__hdl(), rc, data));
}


void USB__PT_PROVIDER::outgoing_send(const USB__get__active__config__descriptor& send_par)
{
	USB_Device *dev = usbdev_by_hdl(send_par.device__hdl());
	struct libusb_config_descriptor *udesc;
	struct USB__descriptor resp;
	OCTETSTRING data;
	int rc;

	struct libusb_device *d = libusb_get_device(dev->mHandle);
	rc = libusb_get_active_config_descriptor(d, &udesc);
	if (rc != 0)
		data = OCTETSTRING();
	else {
		data = OCTETSTRING(udesc->wTotalLength, (const unsigned char *) udesc);
		libusb_free_config_descriptor(udesc);
	}
	incoming_message(USB__descriptor(send_par.req__hdl(), send_par.device__hdl(), rc, data));
}

void USB__PT_PROVIDER::outgoing_send(const USB__get__string__descriptor& send_par)
{
	USB_Device *dev = usbdev_by_hdl(send_par.device__hdl());
	struct USB__descriptor resp;
	unsigned char buf[256];
	OCTETSTRING data;
	int rc;

	rc = libusb_get_string_descriptor(dev->mHandle, send_par.index(), send_par.language__id(),
					  buf, sizeof(buf));
	if (rc < 0)
		data = OCTETSTRING();
	else
		data = OCTETSTRING(rc, buf);
	incoming_message(USB__descriptor(send_par.req__hdl(), send_par.device__hdl(), rc, data));
}




void USB__PT_PROVIDER::outgoing_send(const USB__transfer& send_par)
{
	USB_Transfer *t = new USB_Transfer(this, send_par);
	t->submit();
}


void USB__PT_PROVIDER::transfer_completed(USB_Transfer *t)
{
	USB__transfer__compl xfc;
	xfc.device__hdl() = t->mDev->mID;
	xfc.ttype() = ttype_usb2titan((enum libusb_transfer_type) t->mXfer->type);
	xfc.endpoint() = t->mXfer->endpoint;
	xfc.data() = OCTETSTRING(t->mXfer->length, t->mXfer->buffer);
	xfc.actual__length() = t->mXfer->actual_length;
	incoming_message(xfc);
}

} /* end of namespace */

