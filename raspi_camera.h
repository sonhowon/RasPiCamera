// Copyright 2016

#ifndef RASPICAMERA_RASPI_CAMERA_H_
#define RASPICAMERA_RASPI_CAMERA_H_

// The name of the camera configuration file. Absolute path recommended.
static const LPCTSTR kConfFile = TEXT("C:\\Users\\\SonHoWon\\Desktop\\RasPiCamera\\camera.ini");
// Takes two format arguments. The function name and the error code.
static const char* kErrorMessage = "%s failed with error: 0x%08x\n";
// Takes three format arguments. The function name, the caller function name,
// and the error code.
static const char* kErrorMessageIn = "%s in %s failed with error: 0x%08x\n";
// Takes three format arguments. The function name, the argument value(%s),
// and the error code.
static const char* kErrorMessageFor = "%s for %s failed with error: 0x%08x\n";

// A class that handles jobs related to the Raspberry Pi Camera.
class RasPiCamera {
 public:
  enum RasPiStatus { kOK, kError, kIndexOutOfBounds, kEnd };
  enum ImageStatus { kFree, kBusy };

  // Constructor. address and port are address and port for connection with
  // Raspberry Pi, respectively. address and port both MUST NOT be NULL.
  // If debug is set to true, debug messages will be printed.
  RasPiCamera(const char* address, const char* port, bool debug);
  // Destructor. Waits for the thread to end. Free all the images.
  ~RasPiCamera();
  // Returns the current status.
  RasPiStatus get_status() { return status_; }
  // Request the Raspberry Pi to send len characters from data through its
  // serial output. data MUST NOT be NULL. When error occurs, set status_
  // to be kError and return kError. Otherwise return kOK.
  RasPiStatus RequestSerial(const char* data, int len);
  // Returns a pointer to the IplImage object of the freshest image.
  // If image_to_use_ is kNone, returns NULL.
  IplImage* GetImage(void);

 private:
#pragma pack(push, 1)
   struct RequestProtocol {
     UINT32 header;
     UINT32 data_length;
   };
   struct ReceiveProtocol {
     UINT32 header;
     UINT32 image_size;
   };
#pragma pack(pop)
  enum {
    // header for configuration. the speed of light.
    kConfigure = 299792458,
    // header for receiving image. the first ten digits of pi
    kReceiveImage = 3141592653,
    // header for sending serial requests.
    // the first ten digits of Euler's number
    kRequestSerial = 2718281828,
    // value of image_to_use when no images are ready
    kNone = -1,
    // timedout value for the sockets in milliseconds (10 seconds)
    kTimedout = 10000,
    // maximum tries for timedout. i.e. if (kMaxTimeout + 1)th timeout occurs,
    // it is regarded as an error.
    kMaxTimedout = 30,
    // number of image slots
    kNumberOfImageSlots = 3,
    // number of characters read at once in Config()
    kReadChunk = 1024
  };
  // Connects to the Raspberry Pi. When error occurs, sets status_ to be kError
  // and return kError. Otherwise return kOK.
  RasPiStatus Connect();
  // Configure the camera settings of the Raspberry Pi. The configuration file
  // size must be less than 0xFFFFFFFF. If it is greater, it returns kError.
  // When error occurs, sets status_ to be kError and return kError. Otherwise
  // return kOK.
  RasPiStatus Configure();
  // Fill the input RequestProtocol req with the corresponding header and
  // data_length in network byte order.
  void FillRequestProtocol(UINT32 header, UINT32 data_length,
                           RequestProtocol* req);
  // Change the header and data_length of input ReceiveProtocol recv to host
  // byte order.
  void AnalyzeReceiveProtocol(ReceiveProtocol* recv);
  // Read from config_file and write it on config_string. Sets status_ to
  // kError and returns kError when error occurs. Otherwise return kOK.
  RasPiStatus Read(HANDLE config_file, std::string* config_string);
  // Sends len characters starting from *buf. Wrapper function of send.
  // Keeps on calling send until all of the data is sent. buf MUST NOT be NULL.
  // When error occurs, set status_ to be kError and return kError.
  // Otherwise return kOK.
  RasPiStatus Send(const char* buf, int len);
  // Receives len characters and stores at buf. Wrapper function of recv.
  // Keeps on calling recv until all of the data is received.
  // buf MUST NOT be NULL. When error occurs, set status_ to be kError
  // and return kError. Otherwise return kOK.
  RasPiStatus Recv(char* buf, int len);
  // Calls cvReleaseMat on images_[index].
  // If images_[index] is NULL, nothing happens.
  void ReleaseImage(int index);
  // Safely sets the value of image_status_[index] to be status within
  // critical section. When error occurs, set status_ to be kError and
  // return kError. Otherwise return kOK.
  RasPiStatus SetImageStatus(int index, ImageStatus status);
  // Safely searches for a image to fill within critical section. The image
  // must not be the freshest nor busy. Set index_to_fill to be the index to
  // the image. When error occurs, set status_ to be kError and return kError.
  // Otherwise return kOK. index_to_fill MUST NOT be NULL.
  int FindImageToFill();
  // Safely sets the value of image_to_use_ to be index within critical
  // section. When error occurs, set status_ to be kError and return kError.
  // Otherwise return kOK.
  void SetImageToUse(int index);
  // Function called by image_thread_. Returns FALSE when error occurs.
  // Keeps on receiving images from the Raspberry Pi and update image_status_
  // and image_to_use_.
  static DWORD WINAPI ImageLoop(LPVOID lpParam);

  char address_[40];
  char port_[6];
  bool debug_;
  SOCKET socket_;
  // The current status of the object. If an error occurs, this is set to
  // kError. Before destruction, it is set to kEnd. Otherwise it is set to kOK.
  RasPiStatus status_;
  // A critical section for thread safety.
  CRITICAL_SECTION critical_section_;
  HANDLE image_thread_;
  // Index to the freshest image. It is originally set to be kNone.
  // The value must be handled only within critical section.
  int image_to_use_;
  // image_status_[i] keeps track on the status of images_[i].
  // If either image_thread_ or the main thread is working on the image, it is
  // marked kBusy. Otherwise it is marked kFree.
  // The value must be handled only within critical section.
  ImageStatus image_status_[3];
  CvMat* images_[3];
};

#endif  // RASPICAMERA_RASPI_CAMERA_H_
