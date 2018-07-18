// Copyright 2016

#include <ws2tcpip.h>

RasPiCamera::RasPiStatus RasPiCamera::Connect() {
  if (debug_)
    std::cerr << "Connect()\n";
  WSADATA wsaData;
  struct addrinfo* getaddrinfo_result = NULL;
  struct addrinfo* ptr = NULL;
  struct addrinfo hints;
  int result;
  socket_ = INVALID_SOCKET;
  // Initialize Winsock.
  result = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (result != 0) {
    fprintf_s(stderr, kErrorMessage, "WSAStartup", result);
    status_ = kError;
    return kError;
  }
  ZeroMemory(&hints, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  // Resolve the server address and port.
  result = getaddrinfo(address_, port_, &hints, &getaddrinfo_result);
  if (result != 0) {
    fprintf_s(stderr, kErrorMessage, "getaddrinfo", result);
    status_ = kError;
    return kError;
  }
  // Attempt to connect to an address until one succeeds.
  for (ptr = getaddrinfo_result; ptr != NULL; ptr = ptr->ai_next) {
    socket_ = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
    if (socket_ == INVALID_SOCKET) {
      fprintf_s(stderr, kErrorMessage, "socket", WSAGetLastError());
      status_ = kError;
      return kError;
    }
    result = connect(socket_, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen));
    if (result == SOCKET_ERROR) {
      closesocket(socket_);
      socket_ = INVALID_SOCKET;
      continue;
    }
    break;
  }
  if (debug_) {
    sockaddr socket_address;
    int socket_address_size = sizeof(socket_address);
    getsockname(socket_, &socket_address, &socket_address_size);
    sockaddr_in* socket_address_in =
        reinterpret_cast<sockaddr_in*>(&socket_address);
    std::cerr << "Connected using port "
        << ntohs(socket_address_in->sin_port) << "\n";
  }
  freeaddrinfo(getaddrinfo_result);
  if (socket_ == INVALID_SOCKET) {
    fprintf_s(stderr, "Unable to connect to server!\n");
    status_ = kError;
    return kError;
  }
  DWORD timeout = static_cast<DWORD>(kTimedout);
  int setsockopt_result = setsockopt(
      socket_, SOL_SOCKET, SO_SNDTIMEO,
      reinterpret_cast<char*>(&timeout), sizeof(timeout));
  if (setsockopt_result == SOCKET_ERROR) {
    fprintf_s(stderr, kErrorMessageFor, "setsockopt",
            "SO_SNDTIMEO", WSAGetLastError());
    status_ = kError;
    return kError;
  }
  setsockopt_result = setsockopt(
      socket_, SOL_SOCKET, SO_RCVTIMEO,
      reinterpret_cast<char*>(&timeout), sizeof(timeout));
  if (setsockopt_result == SOCKET_ERROR) {
    fprintf_s(stderr, kErrorMessageFor, "setsockopt",
            "SO_RCVTIMEO", WSAGetLastError());
    status_ = kError;
    return kError;
  }
  return kOK;
}

RasPiCamera::RasPiStatus RasPiCamera::Configure() {
  if (debug_)
    std::cerr << "Configure()\n";
  HANDLE config_file;
  if (debug_)
    std::cerr << "CreateFile(" << kConfFile << ", " << "GENERIC_READ, "
        << "FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, "
        << "NULL)\n";
  config_file = CreateFile(kConfFile, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (config_file == INVALID_HANDLE_VALUE) {
    fprintf_s(stderr, kErrorMessage, "CreateFile", GetLastError());
    status_ = kError;
    return kError;
  }
  LARGE_INTEGER file_size;
  if (GetFileSizeEx(config_file, &file_size) == 0) {
    fprintf_s(stderr, kErrorMessage, "GetFileSizeEx", GetLastError());
    if (CloseHandle(config_file) == 0)
      fprintf(stderr, kErrorMessage, "CloseHandle", GetLastError());
    status_ = kError;
    return kError;
  }
  if (file_size.HighPart != 0) {
    fputs("Configuration file too large.\n", stderr);
    if (CloseHandle(config_file) == 0)
      fprintf(stderr, kErrorMessage, "CloseHandle", GetLastError());
    status_ = kError;
    return kError;
  }
  if (debug_)
    std::cerr << "file_size = " << file_size.QuadPart << "\n";
  std::string config_string;
  if (Read(config_file, &config_string) != kOK) return kError;
  RequestProtocol req;
  FillRequestProtocol(kConfigure, file_size.LowPart, &req);
  RasPiStatus send_result = kError;
  if (Send(reinterpret_cast<char*>(&req), sizeof(req)) == kOK)
    if (Send(config_string.c_str(), file_size.LowPart) == kOK)
      send_result = kOK;
  if (CloseHandle(config_file) == 0) {
    fprintf(stderr, kErrorMessage, "CloseHandle", GetLastError());
    status_ = kError;
    return kError;
  }
  if (send_result != kOK) status_ = send_result;
  return send_result;
}

void RasPiCamera::FillRequestProtocol(UINT32 header, UINT32 data_length,
                                      RequestProtocol* req) {
  req->header = htonl(header);
  req->data_length = htonl(data_length);
}

void RasPiCamera::AnalyzeReceiveProtocol(ReceiveProtocol* recv) {
  UINT32 header = recv->header;
  UINT32 image_size = recv->image_size;
  recv->header = ntohl(header);
  recv->image_size = ntohl(image_size);
}

RasPiCamera::RasPiStatus RasPiCamera::Read(HANDLE config_file,
                                           std::string * config_string) {
  char buf[kReadChunk];
  DWORD bytes_read = 0;
  config_string->clear();
  do {
    if (ReadFile(config_file, buf, kReadChunk, &bytes_read, NULL) == 0) {
      fprintf(stderr, kErrorMessage, "ReadFile", GetLastError());
      status_ = kError;
      return kError;
    }
    config_string->append(buf, bytes_read);
  } while (bytes_read == kReadChunk);
  return kOK;
}

RasPiCamera::RasPiStatus RasPiCamera::Send(const char* buf, int len) {
  if (debug_)
    std::cerr << "Send(" << static_cast<const void*>(buf)
        << ", " << len << ")\n";
  int send_result;
  do {
    if (debug_)
      std::cerr << "send(" << socket_ << ", " << static_cast<const void*>(buf)
          << ", " << len << ", " << 0 << ")\n";
    send_result = send(socket_, buf, len, 0);
    if (debug_)
      std::cerr << "send_result = " << send_result << "\n";
    if (send_result == SOCKET_ERROR) {
      int error = WSAGetLastError();
      if (error == WSAETIMEDOUT) {
        fputs("send timeout occured.\n", stderr);
      } else {
        fprintf_s(stderr, kErrorMessage, "send", error);
      }
      status_ = kError;
      return kError;
    }
    len -= send_result;
    buf += send_result;
  } while (len > 0);
  return kOK;
}

RasPiCamera::RasPiStatus RasPiCamera::Recv(char* buf, int len) {
  if (debug_)
    std::cerr << "Recv(" << static_cast<void*>(buf) << ", " << len << ")\n";
  int recv_result;
  int timedout = 0;
  do {
    if (debug_)
      std::cerr << "recv(" << socket_ << ", " << static_cast<void*>(buf)
          << ", " << len << ", " << 0 << ")\n";
    recv_result = recv(socket_, buf, len, 0);
    if (debug_)
      std::cerr << "recv_result = " << recv_result << "\n";
    if (recv_result == SOCKET_ERROR) {
      int error = WSAGetLastError();
      if (error == WSAETIMEDOUT) {
        fputs("recv timeout keep occured.\n", stderr);
      } else {
        fprintf_s(stderr, kErrorMessage, "recv", error);
      }
      status_ = kError;
      return kError;
    } else if (recv_result == 0) {
      fputs("Connection closed by peer.\n", stderr);
      status_ = kError;
      return kError;
    }
    len -= recv_result;
    buf += recv_result;
  } while (len > 0);
  return kOK;
}

void RasPiCamera::ReleaseImage(int index) {
  if (debug_)
    std::cerr << "ReleaseImage(" << index << ")\n";
  if (images_[index] != NULL)
    cvReleaseMat(&images_[index]);
}

RasPiCamera::RasPiStatus
RasPiCamera::SetImageStatus(int index, ImageStatus status) {
  if (debug_)
    std::cerr << "SetImageStatus(" << index << ", " << status << ")\n";
  if (index < 0 || index >= kNumberOfImageSlots)
    return kIndexOutOfBounds;
  EnterCriticalSection(&critical_section_);
  image_status_[index] = status;
  LeaveCriticalSection(&critical_section_);
  return kOK;
}

int RasPiCamera::FindImageToFill() {
  if (debug_)
    std::cerr << "FindImageToFill()\n";
  int index;
  EnterCriticalSection(&critical_section_);
  // Look for a free slot and mark it as busy.
  for (index = 0; index < kNumberOfImageSlots; ++index) {
    if (index == image_to_use_)
      continue;
    if (image_status_[index] == kFree) {
      image_status_[index] = kBusy;
      break;
    }
  }
  LeaveCriticalSection(&critical_section_);
  return index;
}

void RasPiCamera::SetImageToUse(int index) {
  if (debug_)
    std::cerr << "SetImageToUse(" << index << ")\n";
  EnterCriticalSection(&critical_section_);
  // Mark the status of index as free.
  image_status_[index] = kFree;
  // Set the image_to_use_ to be index.
  image_to_use_ = index;
  LeaveCriticalSection(&critical_section_);
}

DWORD RasPiCamera::ImageLoop(LPVOID lpParam) {
  RasPiCamera* rpic = static_cast<RasPiCamera*>(lpParam);
  if (rpic->debug_)
    std::cerr << "ImageLoop(" << lpParam << ")\n";
  int index_to_fill = kNone;
  while (rpic->status_ != kError) {
    // Check whether the object is in destruction.
    if (rpic->status_ == kEnd) {
      if (rpic->debug_)
        std::cerr << "Status set to kEnd. Exiting loop.\n";
      break;
    }
    index_to_fill = rpic->FindImageToFill();
    if (index_to_fill < 0 || index_to_fill > 2) {
      rpic->status_ = kIndexOutOfBounds;
      return FALSE;
    }
    if (rpic->debug_)
      std::cerr << "index_to_fill = " << index_to_fill << "\n";
    rpic->ReleaseImage(index_to_fill);
    ReceiveProtocol rec;
    RasPiStatus recv_result = rpic->Recv(reinterpret_cast<char*>(&rec),
                                         sizeof(rec));
    if (recv_result != kOK) return FALSE;
    rpic->AnalyzeReceiveProtocol(&rec);
    if (rec.header != rpic->kReceiveImage) {
      rpic->status_ = kError;
      return FALSE;
    }
    UINT32 length = rec.image_size;
    if (rpic->debug_)
      std::cerr << "image_size = " << length << "\n";
    if (length == 0) {
      rpic->status_ = kEnd;
      return kEnd;
    }
    CvMat* image_to_fill = cvCreateMat(1, length, CV_8UC1);
    recv_result = rpic->Recv(reinterpret_cast<char*>(image_to_fill->data.ptr),
                             length);
    if (recv_result != kOK) return FALSE;
    rpic->images_[index_to_fill] = image_to_fill;
    rpic->SetImageToUse(index_to_fill);
  }
  return TRUE;
}

RasPiCamera::RasPiCamera(const char* address, const char* port, bool debug) {
  strncpy_s(address_, address, sizeof(address_) / sizeof(address_[0]));
  strncpy_s(port_, port, sizeof(port_) / sizeof(port_[0]));
  debug_ = debug;
  status_ = kOK;
  if (Connect() != kOK) return;
  if (Configure() != kOK) return;
  // Initiallize images_, image_status_, and image_to_use_.
  for (int index = 0; index < kNumberOfImageSlots; ++index) {
    images_[index] = NULL;
    image_status_[index] = kFree;
  }
  image_to_use_ = kNone;
  if (!InitializeCriticalSectionAndSpinCount(&critical_section_, 0)) {
    status_ = kError;
    fprintf_s(stderr, kErrorMessage, "InitializeCriticalSectionAndSpinCount",
            GetLastError());
    return;
  }
  // Start receiving images.
  DWORD thread_id;
  image_thread_ = CreateThread(
    NULL,         // default security attirubtes
    0,            // default stack size
    (LPTHREAD_START_ROUTINE)ImageLoop,
    this,
    0,            // default creation flags
    &thread_id);  // receive thread identifier
  if (image_thread_ == NULL) {
    fprintf_s(stderr, kErrorMessage, "CreateThread", GetLastError());
    status_ = kError;
    return;
  }
  std::cerr << "Status=" << status_ << "\n";
}

RasPiCamera::~RasPiCamera() {
  status_ = kEnd;   // terminate the loop running on image_thread_
  if (debug_)
    std::cerr << "WaitingForSingleObject(image_thread_, INFINITE)\n";
  WaitForSingleObject(image_thread_, INFINITE);
  if (debug_)
    std::cerr << "CloseHandle(image_thread_)\n";
  CloseHandle(image_thread_);
  DeleteCriticalSection(&critical_section_);
  closesocket(socket_);
  WSACleanup();
  for (int index = 0; index < kNumberOfImageSlots; ++index)
    ReleaseImage(index);
}

RasPiCamera::RasPiStatus RasPiCamera::RequestSerial(const char* data, int len) {
  if (debug_)
    std::cerr << "RequestSerial(" << *data << "P, " << len << ")\n";
  RequestProtocol req;
  FillRequestProtocol(kRequestSerial, len, &req);
  RasPiStatus send_result = Send(reinterpret_cast<char*>(&req), sizeof(req));
  if (send_result != kOK)
    return send_result;
  return Send(data, len);
}

IplImage* RasPiCamera::GetImage(void) {
  if (debug_)
    std::cerr << "GetImage()\n";
  int index;
  EnterCriticalSection(&critical_section_);
  index = image_to_use_;
  // Mark as busy during the decoding process.
  if (index != kNone)
    image_status_[index] = kBusy;
  LeaveCriticalSection(&critical_section_);
  // No images are ready yet.
  if (index == kNone)
    return NULL;
  // Decode the image matrix.
  IplImage* image = cvDecodeImage(images_[index], 1);
  SetImageStatus(index, kFree);
  return image;
}
