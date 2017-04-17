
#ifndef _CRT_SECURE_NO_WARNINGS
    #define _CRT_SECURE_NO_WARNINGS
#endif

#define OUTPUT_BUFFER   (1024 * 64) /* 在於可以加速 printf 及 OutputDebugString 輸出效能 */
//#if !defined(OUTPUT_BUFFER)
    #define USE_FILE_OUT
//#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <ctime>
#include <thread>
#include <unordered_map>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#if defined(_MSC_VER)
    #include <Windows.h>
    #include <io.h>
    #include <errno.h>
#else
    #include <unistd.h>
#endif

#include "Log.h"

namespace kkboylin
{
    namespace log
    {
        namespace
        {
            typedef std::unordered_map< std::string, log::Output > Outputs;

            namespace buffer
            {
                class COutput : public CBufferOutput
                {
                private:
                    struct SBuffer
                    {
                        uint32_t size;
                        char     buffer[1];
                    };
                    typedef std::deque< SBuffer* > Buffers;

                    std::mutex  _Lock;
                    Buffers     _Buffers[2];
                    int         _Index;
                    E_LOG_LEVEL _Level;
                    bool        _Immediately;
                    std::mutex  _LockOutput;

                protected:
                    virtual ~COutput();

                    virtual void OnBegin() { }
                    virtual void OnEnd  () { }
                    virtual void Output (const char* msg, uint32_t size) = 0;

                public:
                    COutput(E_LOG_LEVEL level);

                    virtual void        Output        (E_LOG_LEVEL level, const char* msg, uint32_t size) final;
                    virtual void        Process       () final;
                    virtual E_LOG_LEVEL GetLevel      () const final            { return _Level;        }
                    virtual bool        IsImmediately () const final            { return _Immediately;  }
                    virtual void        SetLevel      (E_LOG_LEVEL value) final { _Level = value;       }
                    virtual void        SetImmediately(bool value) final        { _Immediately = value; }
                };

                COutput::~COutput()
                {
                    for (int i = 0; i < 2; ++i)
                    {
                        Buffers& buffers = _Buffers[i];
                        Buffers::const_iterator it = buffers.begin();
                        for (; it != buffers.end(); ++it)
                            free( (*it) );
                    }
                }

                COutput::COutput(E_LOG_LEVEL level)
                {
                    _Level       = level;
                    _Index       = 0;
                    _Immediately = false;
                }

                void COutput::Output(E_LOG_LEVEL level, const char* msg, uint32_t size)
                {
                    assert(msg != nullptr);
                    assert(size > 0);
                    if (_Level >= level)
                    {
                        if (_Immediately == false)
                        {
                            SBuffer* buffer = (SBuffer*)malloc(sizeof(SBuffer) + size);
                            if (buffer != nullptr)
                            {
                                buffer->size = size;
                                memcpy(buffer->buffer, msg, size);
                                buffer->buffer[size] = 0;
                                _Lock.lock();
                                _Buffers[_Index].push_back(buffer);
                                _Lock.unlock();
                            }
                        }
                        else
                        {
                            _LockOutput.lock();
                            Output(msg, size);
                            _LockOutput.unlock();
                        }
                    }
                }

                void COutput::Process()
                {
                    Buffers& buffers = _Buffers[_Index];
                    if (buffers.size() > 0)
                    {
                        _Lock.lock();
                        if (++_Index > 1)
                            _Index = 0;
                        _Lock.unlock();
                        OnBegin();
                        Buffers::const_iterator it = buffers.begin();
#if defined(OUTPUT_BUFFER)
                        char buffer[OUTPUT_BUFFER];
                        int  index = 0;
                        for (; it != buffers.end(); ++it)
                        {
                            if (index > 0)
                            {
                                if ((index + (*it)->size) >= sizeof(buffer))
                                {
                                    buffer[index] = 0;
                                    _LockOutput.lock();
                                        Output(buffer, index);
                                    _LockOutput.unlock();
                                    index = 0;
                                }
                            }

                            if ((*it)->size >= sizeof(buffer))
                            {
                                _LockOutput.lock();
                                    Output((*it)->buffer, (*it)->size);
                                _LockOutput.unlock();
                            }
                            else
                            {
                                memcpy(&buffer[index], (*it)->buffer, (*it)->size);
                                index += (*it)->size;
                            }
                            free((*it));
                        }
                        if (index > 0)
                        {
                            buffer[index] = 0;
                            _LockOutput.lock();
                                Output(buffer, index);
                            _LockOutput.unlock();
                        }
#else
                        for (; it != buffers.end(); ++it)
                        {
                            _LockOutput.lock();
                                Output((*it)->buffer, (*it)->size);
                            _LockOutput.unlock();
                        }
#endif
                        buffers.clear();
                        OnEnd();
                    }
                }
            };

            namespace console
            {
                class COutput : public buffer::COutput
                {
                protected :
                    virtual void Output(const char* msg, uint32_t size) final;

                public :
                    COutput(E_LOG_LEVEL level) :
                        buffer::COutput(level)
                    {
                    }
                };

                void COutput::Output(const char* msg, uint32_t size)
                {
                    fprintf(stderr, msg);
                }
            }

            namespace debuger
            {
                class COutput : public buffer::COutput
                {
                protected:
                    virtual void Output(const char* msg, uint32_t size) final;

                public:
                    COutput(E_LOG_LEVEL level) :
                        buffer::COutput(level)
                    {
                    }
                };

                void COutput::Output(const char* msg, uint32_t size)
                {
#if defined(_MSC_VER)
                    if (IsDebuggerPresent() == TRUE)
                        OutputDebugString(msg);
#endif
                }
            };

            namespace file
            {
                static bool MkDir(const char* directory)
                {
                    if( (directory != nullptr) &&
                        (*directory != 0) )
                    {
    #if defined(_MSC_VER)
                        return ( ::CreateDirectory(directory, nullptr) == TRUE );
    #else
                        return (mkdir(directory, 0777) == 0);
    #endif
                    }
                    return false;
                };

                static bool IsDirectoryExists(const char* directory)
                {
                    if( (directory != nullptr) &&
                        (*directory != 0) )
                    {
    #if defined(_MSC_VER)
                        DWORD Code = ::GetFileAttributesA(directory);
                        if( (Code != INVALID_FILE_ATTRIBUTES) &&
                            ((Code & FILE_ATTRIBUTE_DIRECTORY) != 0) )
                            return true;
    #else
                        struct stat st;
                        if( stat( (const char *)directory, &st ) != -1 )
                            return (S_ISDIR(st.st_mode) != 0);
    #endif
                    }
                    return	false;
                }

                static bool Rename(const char* oldfilename,
                                   const char* newfilename)
                {
                    if ((oldfilename != NULL) &&
                        (newfilename != NULL) &&
                        (*oldfilename != 0) &&
                        (*newfilename != 0))
#if defined(_MSC_VER)
                        return (::MoveFile(oldfilename, newfilename) == TRUE);
#else
                        return (rename(oldfilename, newfilename) == 0);
#endif
                    return false;
                }

                static bool MkDir(const char* directory, bool createParent)
		        {
			        if( (directory == nullptr) ||
				        (*directory == 0) )
				        return false;
			        if( IsDirectoryExists(directory) == true )
				        return true;

			        if(createParent == true)
			        {
				        /* 建立子目錄 */
				        const char*	p1 = strrchr(directory, '/' );
				        const char*	p2 = strrchr(directory, '\\' );
				        if( (p1 != nullptr) ||
					        (p2 != nullptr) )
				        {
					        const char*	p = p1;
					        if( p2 > p )
						        p = p2;

					        size_t len = (intptr_t)(p - directory);
					        if(len > 0)
					        {
						        std::string	dir( directory, len );
						        if( (dir != ".") &&
							        (dir != "..") )
						        {
							        if(MkDir(dir.c_str(), true ) == false )
								        return false;
						        }
					        }
				        }
			        }
			        return MkDir(directory);
		        }

                class COutput : public buffer::COutput
                {
                protected :
                    std::string _Directory;
                    std::string _Name;
                    std::string _FileName;
                    std::tm     _LastTm;
                    std::time_t _Last;
#if defined(USE_FILE_OUT)
                    FILE*       _File;
#else
                    int         _File;
#endif
                    virtual void Output(const char* msg, uint32_t size) final;

                    bool Reset(std::time_t now);

                    virtual void OnBegin();
                    virtual void OnEnd();

                public :
                    virtual ~COutput();

                    COutput(E_LOG_LEVEL level,
                            const std::string& name,
                            const std::string& directory) :
                        buffer::COutput(level)
                        , _Name(name)
                        , _Directory(directory)
                        , _Last(0)
#if defined(USE_FILE_OUT)
                        , _File(nullptr)
#else
                        , _File(-1)
#endif
                    {
                        _FileName = _Directory + "/" + _Name + ".log";
                    }
                };

                COutput::~COutput()
                {
#if defined(USE_FILE_OUT)
                    if (_File != nullptr)
                        fclose(_File);
#else
                    if (_File != -1)
                        close(_File);
#endif
                }

                bool COutput::Reset(std::time_t now)
                {
                    std::tm val = *std::localtime(&now);
                    if (_Last != 0)
                    {
                        if (val.tm_mday == _LastTm.tm_mday)
                            return true;
                    }
                    if (MkDir(_Directory.c_str(), true) == false)
                        return false;
#if defined(USE_FILE_OUT)
                    if (_File != nullptr)
                    {
                        fclose(_File);
#else
                    if (_File != -1)
                    {
                        close(_File);
#endif
                        char tmp[32];
                        char filename[1024 * 4];
                        std::strftime(tmp, sizeof(tmp), "%Y-%m-%d", &_LastTm);
                        sprintf(filename,
                                "%s/%s-%s.log",
                                _Directory.c_str(),
                                _Name.c_str(),
                                tmp );
                        Rename( _FileName.c_str(), filename );
                    }
#if defined(USE_FILE_OUT)
                    _File = fopen(_FileName.c_str(), "a+b");
                    if (_File == nullptr)
#else
    #if defined(O_BINARY)
                    _File = open(_FileName.c_str(), O_CREAT | O_RDWR | O_APPEND | O_RDWR | O_BINARY);
    #else
                    _File = open(_FileName.c_str(), O_CREAT | O_RDWR | O_APPEND | O_RDWR);
    #endif
                    if (_File == -1)
#endif
                        return false;
                    _LastTm = val;
                    _Last   = now;
                    return true;
                }

                void COutput::OnBegin()
                {
                }

                void COutput::OnEnd()
                {
#if defined(USE_FILE_OUT)
                    fflush(_File);
#endif
                }

                void COutput::Output(const char* msg, uint32_t size)
                {
                    if (_Last == 0)
                    {
                        if( Reset( std::time(nullptr) ) == false )
                            return;
                    }
                    else
                    {
                        std::time_t now = std::time(nullptr);
                        if (_Last != now)
                        {
                            if (Reset(now) == false)
                                return;
                        }
                    }
#if defined(USE_FILE_OUT)
                    fwrite(msg, 1, size, _File);
#else
                    write(_File, msg, size);
#endif
                }
            };

            class CManagerImp : public CManager
            {
                friend std::shared_ptr< CManager >;
            protected :
                std::mutex  _LockProcess;
                Outputs     _Outputs;
                std::mutex  _LockOutput;
                E_LOG_LEVEL _Level;
                bool        _Options[EO_COUNT];

            public:
                CManagerImp(E_LOG_LEVEL level);

                virtual ~CManagerImp();

                virtual void        Append         (const std::string& name, const log::Output& output);
                virtual void        Remove         (const std::string& name);
                virtual E_LOG_LEVEL GetLevel       () const;
                virtual void        SetLevel       (E_LOG_LEVEL value);
                virtual void        Printf         (E_LOG_LEVEL level, const char* fmt, ...);
                virtual void        Process        ();
                virtual bool        EnableOption   (E_OPTIONS option);
                virtual bool        DisableOption  (E_OPTIONS option);
                virtual bool        IsEnabledOption(E_OPTIONS option) const;
                virtual log::Output GetOutput      (const std::string& name);
            };

            void CManagerImp::Append(const std::string& name, const log::Output& output)
            {
                if(output)
                {
                    _LockOutput.lock();
                        _Outputs[name] = output;
                    _LockOutput.unlock();
                }
            }

            bool CManagerImp::EnableOption(E_OPTIONS option)
            {
                if( (option >= EO_TIME) &&
                    (option < EO_COUNT) )
                {
                    _Options[option] = true;
                    return true;
                }
                return false;
            }

            bool CManagerImp::DisableOption(E_OPTIONS option)
            {
                if( (option >= EO_TIME) &&
                    (option < EO_COUNT) )
                {
                    _Options[option] = false;
                    return true;
                }
                return false;
            }

            bool CManagerImp::IsEnabledOption(E_OPTIONS option) const
            {
                if( (option >= EO_TIME) &&
                    (option < EO_COUNT) )
                {
                    return _Options[option];
                }
                return false;
            }

            log::Output CManagerImp::GetOutput(const std::string& name)
            {
                log::Output result;
                if(_Outputs.size() > 0)
                {
                    _LockOutput.lock();
                        Outputs::iterator it = _Outputs.find(name);
                        if (it != _Outputs.end())
                            result = (*it).second;
                    _LockOutput.unlock();
                }
                return result;
            }

            void CManagerImp::Remove(const std::string& name)
            {
                _LockOutput.lock();
                    _Outputs.erase(name);
                _LockOutput.unlock();
            }

            E_LOG_LEVEL CManagerImp::GetLevel() const
            {
                return _Level;
            }

            void CManagerImp::SetLevel(E_LOG_LEVEL value)
            {
                _Level = value;
            }

            static const char* LevelNames[ELL_COUNT] = { "EMERGENCY",
                                                         "ALERT    ",
                                                         "CRITICAL ",
                                                         "ERROR    ",
                                                         "WARNING  ",
                                                         "NOTICE   ",
                                                         "INFO     ",
                                                         "DEBUG    " };

            void CManagerImp::Printf(E_LOG_LEVEL level,
                                     const char* fmt,
                                     ...)
            {
                if( (_Level >= level) &&
                    (_Outputs.size() > 0) )
                {
                    std::time_t now = std::time(nullptr);
                    char buffer[1024 * 8];
                    int  index = 0;
                    if (_Options[EO_DATE] == true)
                        index += strftime(&buffer[index], sizeof(buffer) - (index + 1), "%Y-%m-%d ", std::localtime(&now));
                    else
                    if (_Options[EO_DAY] == true)
                        index += strftime(&buffer[index], sizeof(buffer) - (index + 1), "%d ", std::localtime(&now));
                    if(_Options[EO_TIME] == true)
                        index += strftime( &buffer[index], sizeof(buffer) - (index+1), "%H:%M:%S ", std::localtime(&now) );
                    if (_Options[EO_THREAD] == true)
                    {
                        uint64_t id = std::hash<std::thread::id>()( std::this_thread::get_id() );
                        index += sprintf(&buffer[index], "0x%llx ", id);
                    }
                    if (_Options[EO_LEVEL] == true)
                    {
                        if( (level >= 0) &&
                            (level < ELL_COUNT) )
                        {
                            index += sprintf(&buffer[index], "[%s] ", LevelNames[level]);
                        }
                    }

                    va_list args;
                    va_start(args, fmt);
                    index += vsnprintf( &buffer[index], sizeof(buffer) - (index+1), fmt, args);
                    va_end(args);
                    if (index > 0)
                    {
                        buffer[index] = 0;
                        if(_Outputs.size() > 0)
                        {
                            _LockOutput.lock();
                                Outputs outputs = _Outputs;
                            _LockOutput.unlock();
                            Outputs::const_iterator it = outputs.begin();
                            for (; it != outputs.end(); ++it)
                                (*it).second->Output(level, buffer, index);
                        }
                    }
                }
            }

            CManagerImp::CManagerImp(E_LOG_LEVEL level)
            {
                _Level = level;
                for (int i = 0; i < EO_COUNT; ++i)
                    _Options[i] = false;
            }

            CManagerImp::~CManagerImp()
            {
                Process();
                Process();
            }

            void CManagerImp::Process()
            {
                if (_Outputs.size() > 0)
                {
                    _LockProcess.lock();
                        _LockOutput.lock();
                            Outputs outputs = _Outputs;
                        _LockOutput.unlock();
                        Outputs::const_iterator it = outputs.begin();
                        for (; it != outputs.end(); ++it)
                            (*it).second->Process();
                    _LockProcess.unlock();
                }
            }
        };

        CManager* CManager::_Instance = nullptr;

        CManager::CManager()
        {
            assert(_Instance == nullptr);
            _Instance = this;
        }

        CManager::~CManager()
        {
            _Instance = nullptr;
        }

        Manager Create(E_LOG_LEVEL level)
        {
            return std::make_shared< CManagerImp >(level);
        }

        BufferOutput CreateConsoleOutput(E_LOG_LEVEL level)
        {
            return std::make_shared< console::COutput >(level);
        }

        BufferOutput CreateDebugerOutput(E_LOG_LEVEL level)
        {
            return std::make_shared< debuger::COutput >(level);
        }

        BufferOutput CreateFileOutput(E_LOG_LEVEL level, const std::string& name, const std::string& directory)
        {
            return std::make_shared< file::COutput >(level, name, directory);
        }
    };
};
