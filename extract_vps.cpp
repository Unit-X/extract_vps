//
// Created by Anders Cedronius on 2021-01-25.
//

#include <iostream>
#include <va/va_drm.h>
#include <fcntl.h>
#include <unistd.h>
#include <fstream>
#include <memory>
#include <vector>
#include <algorithm>
#include <cstring>
#include "mfxvideo++.h"

std::unique_ptr<MFXVideoDECODE> gQSVDecoder;

int main() {
    std::cout << "Extract VPS start" << std::endl << std::endl;
    // Open VA
    int lFd = open("/dev/dri/renderD128", O_RDWR);
    if (lFd < 1) {
        std::cout << "Open VA failed." << std::endl;
        return EXIT_FAILURE;
    }
    VADisplay mVADisplay = vaGetDisplayDRM(lFd);
    if (!mVADisplay) {
        close(lFd);
        std::cout << "vaGetDisplayDRM failed." << std::endl;
        return EXIT_FAILURE;
    }
    int lMajor = 1, lMinor = 0;
    VAStatus lStatus = vaInitialize(mVADisplay, &lMajor, &lMinor);
    if (lStatus != VA_STATUS_SUCCESS) {
        close(lFd);
        std::cout << "vaInitialize failed." << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "VA init OK" << std::endl;

    MFXVideoSession lSession;
    mfxStatus sts;
    mfxIMPL lMfxImpl = MFX_IMPL_AUTO_ANY;
    mfxVersion lMfxVer = {
            {34, 1}
    };
    sts = lSession.Init(lMfxImpl, &lMfxVer);
    if (sts != MFX_ERR_NONE) {
        std::cout << "Unable to init MSDK version " << unsigned(lMfxVer.Major) << "." << unsigned(lMfxVer.Minor)
                  << std::endl;
        return EXIT_FAILURE;
    }
    mfxIMPL lImpl;
    sts = lSession.QueryIMPL(&lImpl);
    if (sts != MFX_ERR_NONE) {
        std::cout << "QueryIMPL failed -> " << sts << std::endl;
        return EXIT_FAILURE;
    }
    if (lImpl == MFX_IMPL_SOFTWARE) {
        std::cout << "MSDK implementation in software is not allowed " << std::endl;
        return EXIT_FAILURE;
    } else {
        sts = lSession.SetHandle(MFX_HANDLE_VA_DISPLAY, (mfxHDL) mVADisplay);
        if (sts != MFX_ERR_NONE) {
            std::cout << "SetHandle failed -> " << sts << std::endl;
            return EXIT_FAILURE;
        }
    }
    std::cout << "MSDK init OK" << std::endl << std::endl;

    gQSVDecoder = std::make_unique<MFXVideoDECODE>(lSession);
    if (gQSVDecoder == nullptr) {
        return EXIT_FAILURE;
    }

    //Read bitstream
    std::vector<uint8_t> lHevcData;
    std::ifstream lFile;
    lFile.open ("../source_data.265", std::ios::in | std::ios::binary | std::ios::ate);
    if (!lFile) {
        std::cout << "Unable reading HEVC data-file" << std::endl;
        return EXIT_FAILURE;
    }
    size_t lSize = lFile.tellg();
    lHevcData.resize(lSize);
    lFile.seekg (0, std::ios::beg);
    lFile.read ((char*)lHevcData.data(), lSize);
    lFile.close();

    mfxBitstream lBitStream = {0};
    lBitStream.Data = lHevcData.data();
    lBitStream.DataLength = lHevcData.size();
    lBitStream.MaxLength = lBitStream.DataLength;

    //prepare buffers
    std::vector<uint8_t> lDecodedSpsData(128);
    std::vector<uint8_t> lDecodedPpsData(128);
    std::vector<uint8_t> lDecodedVpsData(128);

    mfxVideoParam lSpeculativeVideoParams = {0};
    mfxExtCodingOptionSPSPPS lSpsPpsOption = {0};
    mfxExtCodingOptionVPS lVpsOption = {0};
    mfxExtBuffer *lpExtendedBuffers[2] = {nullptr};

    //configure mfxExtCodingOptionVPS
    lVpsOption.Header.BufferId = MFX_EXTBUFF_CODING_OPTION_VPS;
    lVpsOption.Header.BufferSz = sizeof(lVpsOption);
    lVpsOption.VPSBuffer = (mfxU8 *) lDecodedVpsData.data();
    lVpsOption.VPSBufSize = lDecodedVpsData.size();
    lpExtendedBuffers[0] = (mfxExtBuffer *) &lVpsOption;

    // configure mfxExtCodingOptionSPSPPS
    lSpsPpsOption.Header.BufferId = MFX_EXTBUFF_CODING_OPTION_SPSPPS;
    lSpsPpsOption.Header.BufferSz = sizeof(lSpsPpsOption);
    lSpsPpsOption.SPSBuffer = (mfxU8 *) lDecodedSpsData.data();
    lSpsPpsOption.SPSBufSize = lDecodedSpsData.size();
    lSpsPpsOption.PPSBuffer = (mfxU8 *) lDecodedPpsData.data();
    lSpsPpsOption.PPSBufSize = lDecodedPpsData.size();
    lpExtendedBuffers[1] = (mfxExtBuffer *) &lSpsPpsOption;

    //Configure the bitstream informateion struct
    lSpeculativeVideoParams.ExtParam = lpExtendedBuffers;
    lSpeculativeVideoParams.NumExtParam = 2;
    lSpeculativeVideoParams.mfx.CodecId = MFX_CODEC_HEVC;
    lSpeculativeVideoParams.IOPattern = MFX_IOPATTERN_OUT_SYSTEM_MEMORY;

    //Get bitstream information
    sts = gQSVDecoder->DecodeHeader(&lBitStream, &lSpeculativeVideoParams);
    if (sts != MFX_ERR_NONE) {
        std::cout <<  "DecodeHeader error." << std::endl;
        return EXIT_FAILURE;
    }

    //Print our findings
    for (int i=0;i<lSpeculativeVideoParams.NumExtParam;i++) {
        mfxExtBuffer *lExtBuf = lSpeculativeVideoParams.ExtParam[i];
        if (lExtBuf->BufferId == MFX_EXTBUFF_CODING_OPTION_SPSPPS) {
            auto *lSpsPps = (mfxExtCodingOptionSPSPPS*)lExtBuf;
            if (lSpsPps->SPSBufSize == 128) {
                std::cout << "did not extract sps" << std::endl;
            } else {
                std::cout << "sps size -> " << unsigned(lSpsPps->SPSBufSize) << std::endl;
            }

            if (lSpsPps->PPSBufSize == 128) {
                std::cout << "did not extract pps" << std::endl;
            } else {
                std::cout << "pps size -> " << unsigned(lSpsPps->PPSBufSize) << std::endl;
            }

        }
        if (lExtBuf->BufferId == MFX_EXTBUFF_CODING_OPTION_VPS) {
            auto *lVps = (mfxExtCodingOptionVPS*)lExtBuf;

            if (lVps->VPSBufSize == 128) {
                std::cout << "did not extract vps" << std::endl;
            } else {
                std::cout << "vps size -> " << unsigned(lVps->VPSBufSize) << std::endl;
            }

        }
    }

    //Close all
    gQSVDecoder->Close();
    lSession.Close();
    close(lFd);
    std::cout<< std::endl << "Extract VPS end" << std::endl;
    return EXIT_SUCCESS;
}