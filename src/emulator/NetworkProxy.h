#ifndef _NETWORK_PROXY_H_
#define _NETWORK_PROXY_H_

#include "EmCommon.h"
#include "networking.pb.h"

class NetworkProxy {
   public:
    NetworkProxy() = default;

    void Open();

    Err Close();

    int OpenCount();

    void SocketOpen(uint8 domain, uint8 type, uint16 protocol);

   private:
    void ConnectSuccess();
    void ConnectAbort();

    void SocketOpenSuccess(uint8* response, size_t size);
    void SocketOpenFail(Err err = netErrInternal);

    MsgRequest NewRequest(pb_size_t payloadTag);
    bool DecodeResponse(uint8* response, size_t size, MsgResponse& msgResponse,
                        pb_size_t payloadTag);

   private:
    uint32 openCount{0};
    uint32 currentId{0xffffffff};

   private:
    NetworkProxy(const NetworkProxy&) = delete;
    NetworkProxy(NetworkProxy&&) = delete;
    NetworkProxy& operator=(const NetworkProxy&) = delete;
    NetworkProxy& operator=(NetworkProxy&&) = delete;
};

extern NetworkProxy& gNetworkProxy;

#endif  // _NETWORK_PROXY_H_