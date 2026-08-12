// main/btc_price.h
#ifndef BTC_PRICE_H
#define BTC_PRICE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化BTC价格获取任务
 * @note 需要在Wi-Fi连接成功后调用
 */
void btc_price_init(void);

/**
 * @brief 获取最新的BTC价格字符串
 * @return const char* 价格字符串（如 "64060.74"），若未获取到则返回"N/A"
 * @note 返回的指针指向内部静态缓冲区，调用者无需释放
 */
const char* btc_price_get_current(void);

/**
 * @brief 检查价格是否已成功获取过
 * @return true 已获取到有效价格，false 尚未获取到
 */
bool btc_price_is_available(void);

#ifdef __cplusplus
}
#endif

#endif // BTC_PRICE_H