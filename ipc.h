//
// Created by dingjing on 11/8/24.
//

#ifndef assistant_IPC_H
#define assistant_IPC_H

#include "3thrd/macros/macros.h"

C_BEGIN_EXTERN_C

typedef enum
{
    IPC_TYPE_NONE                           = 0,
} IpcType;

/**
 * @brief 通信所使用的消息结构
 */
struct __attribute__((packed)) IpcMessage
{
    unsigned int        type;
    unsigned long       dataLen;
    char                data[];
};

C_END_EXTERN_C

#endif // assistant_IPC_H
