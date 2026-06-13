/**
 * @file    SuperLoggerMsg.cpp
 */

#include "SuperLoggerMsg.h"
#include "SuperExternRouter.h"
#include "SuperServer.h"

void SuperLoggerMsgRegister(SuperServer& super)
{
    (void)super;
    /* Logger 转发由 SuperExternRouter SS_EXTERN_FWD 统一处理 */
}
