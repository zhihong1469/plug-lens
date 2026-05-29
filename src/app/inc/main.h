/**
 * @file    main.h
 * @brief   Main application public interface header
 * @details Expose system-level exit trigger function for modules
 * @author  LuoZhihong
 * @license MIT License
 */
#ifndef MAIN_H
#define MAIN_H

#include <stdbool.h>

/**
 * @brief   Trigger system soft exit (Thread-safe, called by business modules)
 */
void app_trigger_soft_exit(void);

#endif /* MAIN_H */