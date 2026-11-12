/**
 * @file        export.h
 * @brief       静态链接及动态库导入导出的符号修饰宏
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#ifndef UVHTTP_EXPORT_H
#define UVHTTP_EXPORT_H

/**
 * @def UVHTTP_EXPORT
 * @brief 动态库导出/导入修饰；静态链接及普通源码集成时为空。
 * @details 定义 UVHTTP_SHARED 启用动态库模式；构建库本身同时定义 UVHTTP_BUILDING_LIBRARY。消费动态库时不能定义 UVHTTP_BUILDING_LIBRARY。
 */
#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(UVHTTP_SHARED)
#if defined(UVHTTP_BUILDING_LIBRARY)
#define UVHTTP_EXPORT __declspec(dllexport)
#else
#define UVHTTP_EXPORT __declspec(dllimport)
#endif
#else
#define UVHTTP_EXPORT
#endif
#else
#if defined(UVHTTP_SHARED) && defined(UVHTTP_BUILDING_LIBRARY)
#define UVHTTP_EXPORT __attribute__((visibility("default")))
#else
#define UVHTTP_EXPORT
#endif
#endif

#endif
