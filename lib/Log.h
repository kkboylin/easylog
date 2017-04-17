
#ifndef __LOG_H__
#define __LOG_H__

#include <string.h>

#include <deque>
#include <string>
#include <memory>
#include <mutex>

namespace kkboylin
{
    namespace log
    {
        enum E_LOG_LEVEL
        {
            ELL_EMERGENCY,	/**< \brief 急. 系統已不可用. */
            ELL_ALERT,		/**< \brief 警報. 應立即修復. */
            ELL_CRITICAL,	/**< \brief 危急. 已在臨界邊緣了, 應立即修復. */
            ELL_ERROR,		/**< \brief 錯誤. 非緊急故障，應轉達給開發人員或管理員. 每個項目必須在給定的時間內解決。 */
            ELL_WARNING,	/**< \brief 警告. 不是錯誤, 但如果不採取行動將發生錯誤. 例如文件系統 85% 滿 - 每個項目必須在給定的時間內解決。 */
            ELL_NOTICE,		/**< \brief 通知. 事件是不尋常的, 但不是錯誤條件, 可歸納在一封電子郵件給開發人員或管理員，以發現潛在的問題 - 無需立即採取行動. */
            ELL_INFO,		/**< \brief 訊息. 正常操作消息 - 可收穫報告，測量吞吐量等 - 無需任何操作. */
            ELL_DEBUG,		/**< \brief 調試. 信息對開發人員有用的操作過程中調試應用程序。*/
            ELL_COUNT
        };

        class COutput
        {
        private :
            COutput                 (const COutput& other) {               }
            const COutput& operator=(const COutput& other) { return *this; }

        protected :
            virtual ~COutput() { }

            COutput() { }

        public :
            virtual void        Output  (E_LOG_LEVEL level, const char* msg, uint32_t size) = 0;
            virtual void        Process () = 0;
            virtual E_LOG_LEVEL GetLevel() const = 0;
            virtual void        SetLevel(E_LOG_LEVEL value) = 0;
        };
        typedef std::shared_ptr< COutput > Output;

        enum E_OPTIONS
        {
            EO_TIME,
            EO_DATE,
            EO_DAY,
            EO_THREAD,
            EO_LEVEL,

            EO_COUNT
        };

        class CManager
        {
            friend std::shared_ptr< CManager >;
        private :
            static CManager* _Instance;

            CManager                 (const CManager& other) {               }
            const CManager& operator=(const CManager& other) { return *this; }

        protected :
            CManager();

            virtual ~CManager();

        public :
            static CManager* GetInstance() { return _Instance; }

            virtual E_LOG_LEVEL GetLevel       () const = 0;
            virtual void        Process        () = 0;
            virtual void        Append         (const std::string& name, const Output& output) = 0;
            virtual void        Remove         (const std::string& name) = 0;
            virtual Output      GetOutput      (const std::string& name) = 0;
            virtual void        SetLevel       (E_LOG_LEVEL value) = 0;
            virtual void        Printf         (E_LOG_LEVEL level, const char* fmt,...) = 0;
            virtual bool        EnableOption   (E_OPTIONS option) = 0;
            virtual bool        DisableOption  (E_OPTIONS option) = 0;
            virtual bool        IsEnabledOption(E_OPTIONS option) const = 0;
        };

        typedef std::shared_ptr< CManager > Manager;

        class CBufferOutput : public COutput
        {
        public :
            virtual bool IsImmediately () const = 0;            
            virtual void SetImmediately(bool value) = 0;
        };

        typedef std::shared_ptr< CBufferOutput > BufferOutput;

        Manager Create(E_LOG_LEVEL level = ELL_INFO);
        BufferOutput CreateConsoleOutput(E_LOG_LEVEL level);
        BufferOutput CreateDebugerOutput(E_LOG_LEVEL level);
        BufferOutput CreateFileOutput   (E_LOG_LEVEL level, const std::string& name, const std::string& directory = "./logs");

        namespace
        {
            static inline int GetFormatLength_(const char* fmt)
            {
                const char* ptr = &fmt[1];
                while (*ptr != 0)
                {
                    switch (*ptr)
                    {
                        case 'd' : /* 10進制 整數 */
                        case 'i' : /* 10進制 整數 */
                        case 'u' : /* 10進制 無號數 */
                        case 'o' : /* 8 進制無號數 */
                        case 'x' : /* 16進制 無號數. 小寫 */
                        case 'X' : /* 16進制 無號數. 大寫 */
                        case 'f' : /* 單精度浮點數(預設輸出精度6位). 小寫 */
                        case 'F' : /* 單精度浮點數(預設輸出精度6位). 大寫 */
                        case 'e' : /* 浮點數使用科學符號表示之,指數將帶正負號. 小寫 */
                        case 'E' : /* 浮點數使用科學符號表示之,指數將帶正負號. 大寫 */
                        case 'g' : /* 由系統決定是否採科學符號表示 : %e or %f */
                        case 'G' : /* 由系統決定是否採科學符號表示 : %E or %F */
                        case 'a' : /* Hexadecimal floating point, lowercase */
                        case 'A' : /* Hexadecimal floating point, uppercase */
                        case 'c' : /* 字元 */
                        case 's' : /* 字串 */
                        case 'p' : /* Pointer address */
                        case 'n' : /* Nothing printed.
                                      The corresponding argument must be a pointer to a signed int.
                                      The number of characters written so far is stored in the pointed location. */
                            return static_cast<int>( (intptr_t)(ptr - fmt) );

                        case 'l' :
                        case 'L' :
                        case 'I' :
                        case 'P' :
                        case 'R' :
                        case 'q' :
                        case '#' :
                        case '+' :
                        case '-' :
                        case '*' :
                        case '.' :
                        case '0' :
                        case '1' :
                        case '2' :
                        case '3' :
                        case '4' :
                        case '5' :
                        case '6' :
                        case '7' :
                        case '8' :
                        case '9' :
                            ++ptr;
                            break;

                        default:
                            return -1;
                    }
                }
                return -1;
            }

            template< typename T >
            static void ValueOutput_(std::string& output, const char*& fmt, const T& value)
            {
                int count = GetFormatLength_(fmt);
                if (count > 0)
                {
                    char        buffer[1024 * 8];
                    std::string format(fmt, count + 1);
                    int         idx = snprintf(buffer, sizeof(buffer) - 1, format.c_str(), value);
                    if (idx > 0)
                    {
                        buffer[idx] = 0;
                        output += buffer;
                    }
                    fmt += (count + 1);
                }
                else
                {
                    fmt += strlen(fmt);
                }
            }

            static void ValueOutput_(std::string& output, const char*& fmt, const std::string& value)
            {
                int count = GetFormatLength_(fmt);
                if (count > 0)
                {
                    if (count == 1)
                    {
                        output += value;
                    }
                    else
                    {
                        char        buffer[1024 * 8];
                        std::string format(fmt, count + 1);
                        int         idx = snprintf(buffer, sizeof(buffer) - 1, format.c_str(), value.c_str());
                        if (idx > 0)
                        {
                            buffer[idx] = 0;
                            output += buffer;
                        }
                    }
                    fmt += (count + 1);
                }
                else
                {
                    fmt += strlen(fmt);
                }
            }

            static void ValueOutput_(std::string& output, const char*& fmt, const char* value)
            {
                int count = GetFormatLength_(fmt);
                if (count > 0)
                {
                    if (count == 1)
                    {
                        output += value;
                    }
                    else
                    {
                        char        buffer[1024 * 8];
                        std::string format(fmt, count + 1);
                        int         idx = snprintf(buffer, sizeof(buffer) - 1, format.c_str(), value);
                        if (idx > 0)
                        {
                            buffer[idx] = 0;
                            output += buffer;
                        }
                    }
                    fmt += (count + 1);
                }
                else
                {
                    fmt += strlen(fmt);
                }
            }

            static void LogOutput(E_LOG_LEVEL level, const char* format) // base function
            {
                if (CManager::GetInstance()->GetLevel() >= level)
                    CManager::GetInstance()->Printf(level, format);
            }

            static void LogOutput(E_LOG_LEVEL level, const std::string& format)
            {
                if (CManager::GetInstance()->GetLevel() >= level)
                    CManager::GetInstance()->Printf(level, format.c_str());
            }

            static void _LogOutput(E_LOG_LEVEL level, std::string& output, const char* format)
            {
                output += format;
            }

            static void _LogOutput(E_LOG_LEVEL level, std::string& output, const std::string& format)
            {
                output += format;
            }

            template<typename T, typename... Targs>
            static void _LogOutput(E_LOG_LEVEL level,
                                   std::string& output,
                                   const char* format,
                                   T value, Targs... Fargs)
            {
                if (CManager::GetInstance()->GetLevel() >= level)
                {
                    for (; *format != '\0'; format++)
                    {
                        if (*format == '%')
                        {
                            if (format[1] != '%')
                            {
                                ValueOutput_(output, format, value);
                                _LogOutput(level, output, format, Fargs...);
                                break;
                            }
                        }
                        output += *format;
                    }
                }
            }

            template<typename T, typename... Targs>
            static void LogOutput(E_LOG_LEVEL level, const char* format, T value, Targs... Fargs)
            {
                if (CManager::GetInstance()->GetLevel() >= level)
                {
                    std::string output;
                    for (; *format != '\0'; format++)
                    {
                        if (*format == '%')
                        {
                            if (format[1] != '%')
                            {
                                ValueOutput_(output, format, value);
                                _LogOutput(level, output, format, Fargs...);
                                break;
                            }
                        }
                        output += *format;
                    }
                    CManager::GetInstance()->Printf( level, output.c_str() );
                }
            }
        }
    };
};

#endif // __LOG_H__
