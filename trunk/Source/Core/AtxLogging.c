/*****************************************************************
|
|      File: AtxLogging.c
|
|      Atomix - Logging Support
|
|      (c) 2002-2006 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
****************************************************************/
/** @file
* Implementation file for logging
*/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include <stdarg.h>
#include "AtxConfig.h"
#include "AtxDebug.h"
#include "AtxTypes.h"
#include "AtxUtils.h"
#include "AtxResults.h"
#include "AtxLogging.h"
#include "AtxSystem.h"
#include "AtxString.h"
#include "AtxList.h"
#include "AtxDataBuffer.h"
#include "AtxFile.h"
#include "AtxStreams.h"
#include "AtxReferenceable.h"
#include "AtxDestroyable.h"
#include "AtxSockets.h"

/*----------------------------------------------------------------------
|   types
+---------------------------------------------------------------------*/
typedef struct {
    ATX_String key;
    ATX_String value;
} ATX_LogConfigEntry;

typedef struct {
    ATX_List*   config;
    ATX_List*   loggers;
    ATX_Logger* root;
    ATX_Boolean initialized;
} ATX_LogManager;

typedef struct {
    ATX_Boolean       use_colors;
    ATX_OutputStream* stream;
} ATX_LogFileHandler;

typedef struct {
    ATX_String        host;
    ATX_UInt16        port;
    ATX_OutputStream* stream;
} ATX_LogTcpHandler;

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
#define ATX_LOG_HEAP_BUFFER_INCREMENT 4096
#define ATX_LOG_STACK_BUFFER_MAX_SIZE 512
#define ATX_LOG_HEAP_BUFFER_MAX_SIZE  65536

#if !defined(ATOMIX_LOG_CONFIG)
#define ATX_LOG_CONFIG_ENV "ATOMIX_LOG_CONFIG"
#endif

#if !defined(ATX_LOG_DEFAULT_CONFIG_SOURCE)
#define ATX_LOG_DEFAULT_CONFIG_SOURCE "file:atomix-logging.properties"
#endif

#define ATX_LOG_ROOT_DEFAULT_LOG_LEVEL ATX_LOG_LEVEL_INFO
#define ATX_LOG_ROOT_DEFAULT_HANDLER   "ConsoleHandler"
#if !defined(ATX_LOG_ROOT_DEFAULT_FILE_HANDLER_FILENAME)
#define ATX_LOG_ROOT_DEFAULT_FILE_HANDLER_FILENAME "_atomix.log"
#endif

#define ATX_LOG_TCP_HANDLER_DEFAULT_PORT            7723
#define ATX_LOG_TCP_HANDLER_DEFAULT_CONNECT_TIMEOUT 5000 /* 5 seconds */

#if defined(WIN32)
#define ATX_LOG_CONSOLE_HANDLER_DEFAULT_COLOR_MODE ATX_FALSE
#else
#define ATX_LOG_CONSOLE_HANDLER_DEFAULT_COLOR_MODE ATX_TRUE
#endif

/*----------------------------------------------------------------------
|   globals
+---------------------------------------------------------------------*/
static ATX_LogManager LogManager;

/*----------------------------------------------------------------------
|   forward references
+---------------------------------------------------------------------*/
static ATX_Logger* ATX_Logger_Create(const char* name);
static ATX_Result ATX_Logger_Destroy(ATX_Logger* self);
static ATX_Result ATX_LogFileHandler_Create(const char*     logger_name, 
                                            ATX_Boolean     use_console,
                                            ATX_LogHandler* handler);
static ATX_Result ATX_LogTcpHandler_Create(const char*     logger_name, 
                                           ATX_LogHandler* handler);

/*----------------------------------------------------------------------
|   ATX_LogHandler_Create
+---------------------------------------------------------------------*/
static ATX_Result
ATX_LogHandler_Create(const char*     logger_name,
                      const char*     handler_name, 
                      ATX_LogHandler* handler)
{
    if (ATX_StringsEqual(handler_name, "FileHandler")) {
        return ATX_LogFileHandler_Create(logger_name, ATX_FALSE, handler);
    } else if (ATX_StringsEqual(handler_name, "ConsoleHandler")) {
        return ATX_LogFileHandler_Create(logger_name, ATX_TRUE, handler);
    } else if (ATX_StringsEqual(handler_name, "TcpHandler")) {
        return ATX_LogTcpHandler_Create(logger_name, handler);
    }

    return ATX_ERROR_NO_SUCH_CLASS;
}

/*----------------------------------------------------------------------
|   ATX_Log_GetLogLevel
+---------------------------------------------------------------------*/
int 
ATX_Log_GetLogLevel(const char* name)
{
    if (       ATX_StringsEqual(name, "SEVERE")) {
        return ATX_LOG_LEVEL_SEVERE;
    } else if (ATX_StringsEqual(name, "WARNING")) {
        return ATX_LOG_LEVEL_WARNING;
    } else if (ATX_StringsEqual(name, "INFO")) {
        return ATX_LOG_LEVEL_INFO;
    } else if (ATX_StringsEqual(name, "FINE")) {
        return ATX_LOG_LEVEL_FINE;
    } else if (ATX_StringsEqual(name, "FINER")) {
        return ATX_LOG_LEVEL_FINER;
    } else if (ATX_StringsEqual(name, "FINEST")) {
        return ATX_LOG_LEVEL_FINEST;
    } else if (ATX_StringsEqual(name, "ALL")) {
        return ATX_LOG_LEVEL_ALL;
    } else if (ATX_StringsEqual(name, "OFF")) {
        return ATX_LOG_LEVEL_OFF;
    } else {
        return -1;
    }
}

/*----------------------------------------------------------------------
|   ATX_Log_GetLogLevelName
+---------------------------------------------------------------------*/
const char*
ATX_Log_GetLogLevelName(int level)
{
    switch (level) {
        case ATX_LOG_LEVEL_SEVERE:  return "SEVERE";
        case ATX_LOG_LEVEL_WARNING: return "WARNING";
        case ATX_LOG_LEVEL_INFO:    return "INFO";
        case ATX_LOG_LEVEL_FINE:    return "FINE";
        case ATX_LOG_LEVEL_FINER:   return "FINER";
        case ATX_LOG_LEVEL_FINEST:  return "FINEST";
        case ATX_LOG_LEVEL_OFF:     return "OFF";
        default:                    return "";
    }
}

/*----------------------------------------------------------------------
|   ATX_Log_GetLogLevelAnsiColor
+---------------------------------------------------------------------*/
static const char*
ATX_Log_GetLogLevelAnsiColor(int level)
{
    switch (level) {
        case ATX_LOG_LEVEL_SEVERE:  return "31";
        case ATX_LOG_LEVEL_WARNING: return "33";
        case ATX_LOG_LEVEL_INFO:    return "32";
        case ATX_LOG_LEVEL_FINE:    return "34";
        case ATX_LOG_LEVEL_FINER:   return "35";
        case ATX_LOG_LEVEL_FINEST:  return "36";
        default:                    return NULL;
    }
}

/*----------------------------------------------------------------------
|   ATX_LogManager_GetConfigValue
+---------------------------------------------------------------------*/
static ATX_String*
ATX_LogManager_GetConfigValue(const char* prefix, const char* suffix)
{
    ATX_ListItem* item = ATX_List_GetFirstItem(LogManager.config);
    ATX_Size      prefix_length = prefix?ATX_StringLength(prefix):0;
    ATX_Size      suffix_length = suffix?ATX_StringLength(suffix):0;
    ATX_Size      key_length    = prefix_length+suffix_length;
    while (item) {
        ATX_LogConfigEntry* entry = (ATX_LogConfigEntry*)ATX_ListItem_GetData(item);
        if (ATX_String_GetLength(&entry->key) == key_length &&
            (prefix == NULL || ATX_String_StartsWith(&entry->key, prefix)) &&
            (suffix == NULL || ATX_String_EndsWith(&entry->key, suffix  )) ) {
                return &entry->value;
            }
            item = ATX_ListItem_GetNext(item);
    }

    /* not found */
    return NULL;
}

/*----------------------------------------------------------------------
|   ATX_LogManager_SetConfigValue
+---------------------------------------------------------------------*/
static ATX_Result
ATX_LogManager_SetConfigValue(const char* key, const char* value)
{
    ATX_String* value_string = ATX_LogManager_GetConfigValue(key, NULL);
    if (value_string) {
        /* the key already exists, replace the value */
        /*ATX_Debug("ATX_LogManager_SetConfigValue - update, key=%s, value=%s\n", 
                  key, value);*/
        return ATX_String_Assign(value_string, value);
    } else {
        /* the value does not already exist, create a new one */
        ATX_Result result;
        ATX_LogConfigEntry* entry = ATX_AllocateMemory(sizeof(ATX_LogConfigEntry));
        if (entry == NULL) return ATX_ERROR_OUT_OF_MEMORY;
        result = ATX_List_AddData(LogManager.config, (void*)entry);
        if (ATX_FAILED(result)) {
            ATX_FreeMemory((void*)entry);
            return result;
        }
        entry->key = ATX_String_Create(key);
        entry->value = ATX_String_Create(value);
        /*ATX_Debug("ATX_LogManager_SetConfigValue - new entry, key=%s, value=%s\n", 
                  key, value);*/
    }

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   ATX_LogManager_ClearConfig
+---------------------------------------------------------------------*/
static ATX_Result
ATX_LogManager_ClearConfig() 
{
    ATX_ListItem* item = ATX_List_GetFirstItem(LogManager.config);
    while (item) {
        ATX_LogConfigEntry* entry = (ATX_LogConfigEntry*)ATX_ListItem_GetData(item);
        ATX_String_Destruct(&entry->key);
        ATX_String_Destruct(&entry->value);
        ATX_FreeMemory((void*)entry);
        item = ATX_ListItem_GetNext(item);
    }
    ATX_List_Clear(LogManager.config);

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   ATX_LogManager_ParseConfig
+---------------------------------------------------------------------*/
static ATX_Result
ATX_LogManager_ParseConfig(const char* config,
                           ATX_Size    config_size) 
{
    const char* cursor = config;
    const char* line = config;
    const char* separator = NULL;

    /* parse all entries */
    while (cursor <= config+config_size) {
        /* separators are newlines, ';' or end of buffer */
        if (*cursor == '\n' || 
            *cursor == '\r' || 
            *cursor == ';' || 
            cursor == config+config_size) {
            /* newline or end of buffer */
            if (separator && line[0] != '#') {
                /* we have a property */
                ATX_String key   = ATX_String_CreateFromSubString(line, 0, (ATX_Size)(separator-line));
                ATX_String value = ATX_String_CreateFromSubString(line, (ATX_Size)(separator+1-line), (ATX_Size)(cursor-(separator+1)));
                ATX_String_TrimWhitespace(&key);
                ATX_String_TrimWhitespace(&value);
            
                ATX_LogManager_SetConfigValue(ATX_CSTR(key), ATX_CSTR(value));
            }
            line = cursor+1;
            separator = NULL;
        } else if (*cursor == '=' && separator == NULL) {
            separator = cursor;
        }
        cursor++;
    }

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   ATX_LogManager_ParseConfigFile
+---------------------------------------------------------------------*/
static ATX_Result
ATX_LogManager_ParseConfigFile(const char* filename) 
{
    ATX_Result result;

    /* load the file */
    ATX_DataBuffer* buffer = NULL;
    ATX_CHECK(ATX_LoadFile(filename, &buffer));

    /* parse the config */
    result = ATX_LogManager_ParseConfig((const char*)ATX_DataBuffer_GetData(buffer),
                                        ATX_DataBuffer_GetDataSize(buffer));

    /* destroy the buffer */
    ATX_DataBuffer_Destroy(buffer);

    return result;
}

/*----------------------------------------------------------------------
|   ATX_LogManager_ParseConfigSource
+---------------------------------------------------------------------*/
static ATX_Result
ATX_LogManager_ParseConfigSource(ATX_String* source) 
{
    if (ATX_String_StartsWith(source, "file:")) {
        /* file source */
        ATX_LogManager_ParseConfigFile(ATX_CSTR(*source)+5);
    } else if (ATX_String_StartsWith(source, "plist:")) {
        ATX_LogManager_ParseConfig(ATX_CSTR(*source)+6,
                                   ATX_String_GetLength(source)-6);
    } else {
        return ATX_ERROR_INVALID_SYNTAX;
    }

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   ATX_LogManager_Terminate
+---------------------------------------------------------------------*/
static void
ATX_LogManager_Terminate(void)
{
    /* destroy everything we've created */
    ATX_LogManager_ClearConfig();
    ATX_List_Destroy(LogManager.config);

    {
        ATX_ListItem* item = ATX_List_GetFirstItem(LogManager.loggers);
        ATX_Logger* logger = (ATX_Logger*)ATX_ListItem_GetData(item);
        ATX_Logger_Destroy(logger);
        item = ATX_ListItem_GetNext(item);
    }
    ATX_List_Destroy(LogManager.loggers);
    ATX_Logger_Destroy(LogManager.root);
}

/*----------------------------------------------------------------------
|   ATX_LogManager_HaveLoggerConfig
+---------------------------------------------------------------------*/
static ATX_Boolean
ATX_LogManager_HaveLoggerConfig(const char* name)
{
    ATX_ListItem* item = ATX_List_GetFirstItem(LogManager.config);
    ATX_Size      name_length = ATX_StringLength(name);
    while (item) {
        ATX_LogConfigEntry* entry = (ATX_LogConfigEntry*)ATX_ListItem_GetData(item);
        if (ATX_String_StartsWith(&entry->key, name)) {
            const char* suffix = ATX_CSTR(entry->key)+name_length;
            if (ATX_StringsEqual(suffix, ".level") ||
                ATX_StringsEqual(suffix, ".handlers") ||
                ATX_StringsEqual(suffix, ".forward")) {
                return ATX_TRUE;
            }
        }
        item = ATX_ListItem_GetNext(item);
    }

    /* no config found */
    return ATX_FALSE;

}

/*----------------------------------------------------------------------
|   ATX_LogManager_ConfigureLogger
+---------------------------------------------------------------------*/
static ATX_Result
ATX_LogManager_ConfigureLogger(ATX_Logger* logger)
{
    /* configure the level */
    {
        ATX_String* level_value = ATX_LogManager_GetConfigValue(
            ATX_CSTR(logger->name),".level");
        if (level_value) {
            long value;
            /* try a symbolic name */
            value = ATX_Log_GetLogLevel(ATX_CSTR(*level_value));
            if (value < 0) {
                /* try a numeric value */
                if (ATX_FAILED(ATX_String_ToInteger(level_value, &value, ATX_FALSE))) {
                    value = -1;
                }
            }
            if (value >= 0) {
                logger->level = value;
                logger->level_is_inherited = ATX_FALSE;
            }
        }
    }

    /* configure the handlers */
    {
        ATX_String* handlers = ATX_LogManager_GetConfigValue(
            ATX_CSTR(logger->name),".handlers");
        if (handlers) {
            const char*    handlers_list = ATX_CSTR(*handlers);
            const char*    cursor = handlers_list;
            const char*    name_start = handlers_list;
            ATX_String     handler_name = ATX_EMPTY_STRING;
            ATX_LogHandler handler;
            for (;;) {
                if (*cursor == '\0' || *cursor == ',') {
                    if (cursor != name_start) {
                        ATX_String_AssignN(&handler_name, name_start, (ATX_Size)(cursor-name_start));
                        ATX_String_TrimWhitespace(&handler_name);
                        
                        /* create a handler */
                        if (ATX_SUCCEEDED(
                            ATX_LogHandler_Create(ATX_CSTR(logger->name),
                                                  ATX_CSTR(handler_name),
                                                  &handler))) {
                            ATX_Logger_AddHandler(logger, &handler);
                        }

                    }
                    if (*cursor == '\0') break;
                    name_start = cursor+1;
                }
                ++cursor;
            }
            ATX_String_Destruct(&handler_name);
        }
    }

    /* configure the forwarding */
    {
        ATX_String* forward = ATX_LogManager_GetConfigValue(
            ATX_CSTR(logger->name),".forward");
        if (forward) {
            if (ATX_String_Compare(forward, "false", ATX_TRUE) == 0||
                ATX_String_Compare(forward, "no", ATX_FALSE) == 0  ||
                ATX_String_Compare(forward, "0", ATX_FALSE) == 0) {
                logger->forward_to_parent = ATX_FALSE;
            }
        }
    }

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   ATX_LogManager_Init
+---------------------------------------------------------------------*/
static void
ATX_LogManager_Init(void) 
{
    char* config_sources;

    /* register a function to be called when the program exits */
    ATX_AtExit(ATX_LogManager_Terminate);

    /* create a logger list */
    ATX_List_Create(&LogManager.loggers);

    /* create a config */
    ATX_List_Create(&LogManager.config);

    /* decide what the configuration sources are */
    config_sources = ATX_GetEnvironment(ATX_LOG_CONFIG_ENV);
    if (config_sources == NULL) config_sources = ATX_LOG_DEFAULT_CONFIG_SOURCE;

    /* load all configs */
    {
        ATX_String config_source = ATX_EMPTY_STRING;
        const char* cursor = config_sources; 
        const char* source = config_sources;
        for (;;) {
            if (*cursor == '\0' || *cursor == '|') {
                if (cursor != source) {
                    ATX_String_AssignN(&config_source, source, (ATX_Size)(cursor-source));
                    ATX_String_TrimWhitespace(&config_source);
                    ATX_LogManager_ParseConfigSource(&config_source);
                }
                if (*cursor == '\0') break;
            }
            cursor++;
        }
        ATX_String_Destruct(&config_source);
    }

    /* create the root logger */
    LogManager.root = ATX_Logger_Create("");
    if (LogManager.root) {
        LogManager.root->level = ATX_LOG_ROOT_DEFAULT_LOG_LEVEL;
        LogManager.root->level_is_inherited = ATX_FALSE;
        ATX_LogManager_SetConfigValue(".handlers", 
                                      ATX_LOG_ROOT_DEFAULT_HANDLER);
        ATX_LogManager_ConfigureLogger(LogManager.root);
    }

    /* we are now initialized */
    LogManager.initialized = ATX_TRUE;
}

/*----------------------------------------------------------------------
|   ATX_Logger_Create
+---------------------------------------------------------------------*/
static ATX_Logger*
ATX_Logger_Create(const char* name)
{
    /* create a new logger */
    ATX_Logger* logger = 
        (ATX_Logger*)ATX_AllocateZeroMemory(sizeof(ATX_Logger));
    if (logger == NULL) return NULL;

    /* setup the logger */
    logger->level              = ATX_LOG_LEVEL_OFF;
    logger->level_is_inherited = ATX_TRUE;
    logger->name               = ATX_String_Create(name);
    logger->forward_to_parent  = ATX_TRUE;
    logger->parent             = NULL;

    return logger;
}

/*----------------------------------------------------------------------
|   ATX_Logger_Destroy
+---------------------------------------------------------------------*/
static ATX_Result
ATX_Logger_Destroy(ATX_Logger* self)
{
    /* destroy all handlers */
    ATX_LogHandlerEntry* entry = self->handlers;
    while (entry) {
        entry->handler.iface->Destroy(&entry->handler);
        entry = entry->next;
    }
    
    /* destruct other members */
    ATX_String_Destruct(&self->name);

    /* free the object memory */
    ATX_FreeMemory((void*)self);

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   ATX_Logger_Log
+---------------------------------------------------------------------*/
void
ATX_Logger_Log(ATX_Logger*  self, 
               int          level, 
               const char*  source_file,
               unsigned int source_line,
               const char*  msg, 
                            ...)
{
    char     buffer[ATX_LOG_STACK_BUFFER_MAX_SIZE];
    ATX_Size buffer_size = sizeof(buffer);
    char*    message = buffer;
    int      result;
    va_list  args;

    va_start(args, msg);

    for(;;) {
        /* try to format the message (it might not fit) */
        result = ATX_FormatStringVN(message, buffer_size-1, msg, args);
        message[buffer_size-1] = 0; /* force a NULL termination */
        if (result >= 0) {
            /* the message is formatted, publish it to the handlers */
            ATX_LogRecord record;
            
            /* setup the log record */
            record.logger_name = ATX_CSTR(self->name),
            record.level       = level;
            record.message     = message;
            record.source_file = source_file;
            record.source_line = source_line;
            ATX_System_GetCurrentTimeStamp(&record.timestamp);

            /* call all handlers for this logger and parents */
            {
                ATX_Logger* logger = self;
                while (logger) {
                    /* call all handlers for the current logger */
                    ATX_LogHandlerEntry* entry = logger->handlers;
                    while (entry) {
                        entry->handler.iface->Log(&entry->handler, &record);
                        entry = entry->next;
                    }

                    /* forward to the parent unless this logger does not forward */
                    if (logger->forward_to_parent) {
                        logger = logger->parent;
                    } else {
                        break;
                    }
                }
            }
            break;
        }

        /* the buffer was too small, try something bigger */
        buffer_size = (buffer_size+ATX_LOG_HEAP_BUFFER_INCREMENT)*2;
        if (buffer_size > ATX_LOG_HEAP_BUFFER_MAX_SIZE) break;
        if (message != buffer) ATX_FreeMemory((void*)message);
        message = ATX_AllocateMemory(buffer_size);
        if (message == NULL) return;
    }

    /* free anything we may have allocated */
    if (message != buffer) ATX_FreeMemory((void*)message);

    va_end(args);
}

/*----------------------------------------------------------------------
|   ATX_Logger_AddHandler
+---------------------------------------------------------------------*/
ATX_Result
ATX_Logger_AddHandler(ATX_Logger* self, ATX_LogHandler* handler)
{
    ATX_LogHandlerEntry* entry;

    /* check parameters */
    if (handler == NULL) return ATX_ERROR_INVALID_PARAMETERS;

    /* allocate a new entry */
    entry = (ATX_LogHandlerEntry*)ATX_AllocateMemory(sizeof(ATX_LogHandlerEntry));
    if (entry == NULL) return ATX_ERROR_OUT_OF_MEMORY;

    /* setup the entry */
    entry->handler = *handler;
    
    /* attach the new entry at the beginning of the list */
    entry->next = self->handlers;
    self->handlers = entry;

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   ATX_Logger_SetParent
+---------------------------------------------------------------------*/
static ATX_Result
ATX_Logger_SetParent(ATX_Logger* self, ATX_Logger* parent)
{
    ATX_Logger* logger = self;

    /* set our new parent */
    self->parent = parent;

    /* find the first ancestor with its own log level */
    if (logger->level_is_inherited && logger->parent) {
        logger = logger->parent;
    }
    if (logger != self) self->level = logger->level;

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   ATX_Log_FindLogger
+---------------------------------------------------------------------*/
static ATX_Logger*
ATX_Log_FindLogger(const char* name)
{
    ATX_ListItem* item = ATX_List_GetFirstItem(LogManager.loggers);
    while (item) {
        ATX_Logger* logger = (ATX_Logger*)ATX_ListItem_GetData(item);
        if (ATX_StringsEqual(ATX_CSTR(logger->name), name)) {
            return logger;
        }
        item = ATX_ListItem_GetNext(item);
    }

    return NULL;
}

/*----------------------------------------------------------------------
|   ATX_Log_GetLogger
+---------------------------------------------------------------------*/
ATX_Logger*
ATX_Log_GetLogger(const char* name)
{
    ATX_Logger* logger;

    /* check that the manager is initialized */
    if (!LogManager.initialized) {
        /* init the manager */
        ATX_LogManager_Init();
        ATX_ASSERT(LogManager.initialized);
    }

    /* check if this logger is already configured */
    logger = ATX_Log_FindLogger(name);
    if (logger) return logger;

    /* create a new logger */
    logger = ATX_Logger_Create(name);
    if (logger == NULL) return NULL;

    /* configure the logger */
    ATX_LogManager_ConfigureLogger(logger);

    /* find which parent to attach to */
    {
        ATX_Logger* parent = LogManager.root;
        ATX_String  parent_name = ATX_String_Create(name);
        for (;;) {
            ATX_Logger* candidate_parent;

            /* find the last dot */
            int dot = ATX_String_ReverseFindChar(&parent_name, '.');
            if (dot < 0) break;
            ATX_String_SetLength(&parent_name, dot);
            
            /* see if the parent exists */
            candidate_parent = ATX_Log_FindLogger(ATX_CSTR(parent_name));
            if (candidate_parent) {
                parent = candidate_parent;
                break;
            }

            /* this parent name does not exist, see if we need to create it */
            if (ATX_LogManager_HaveLoggerConfig(ATX_CSTR(parent_name))) {
                parent = ATX_Log_GetLogger(ATX_CSTR(parent_name));
                break;
            }
        }
        ATX_String_Destruct(&parent_name);

        /* attach to the parent */
        ATX_Logger_SetParent(logger, parent);
    }

    /* add this logger to the list */
    ATX_List_AddData(LogManager.loggers, (void*)logger);

    return logger;
}

/*----------------------------------------------------------------------
|   ATX_LogFileHandler forward references
+---------------------------------------------------------------------*/
static const ATX_LogHandlerInterface ATX_LogFileHandler_Interface;

/*----------------------------------------------------------------------
|   ATX_LogFileHandler_Log
+---------------------------------------------------------------------*/
static void
ATX_LogFileHandler_Log(ATX_LogHandler* _self, const ATX_LogRecord* record)
{
    ATX_LogFileHandler* self = (ATX_LogFileHandler*)_self->instance;
    const char*         level_name = ATX_Log_GetLogLevelName(record->level);
    char                level_string[16];
    char                buffer[64];
    const char*         ansi_color = NULL;

    /* format and emit the record */
    if (level_name[0] == '\0') {
        ATX_IntegerToString(record->level, level_string, sizeof(level_string));
        level_name = level_string;
    }
    ATX_OutputStream_Write(self->stream, "[", 1, NULL);
    ATX_OutputStream_WriteString(self->stream, record->logger_name);
    ATX_OutputStream_Write(self->stream, "] ", 2, NULL);
    ATX_OutputStream_WriteString(self->stream, record->source_file);
    ATX_OutputStream_Write(self->stream, " ", 1, NULL);
    ATX_IntegerToStringU(record->timestamp.seconds, buffer, sizeof(buffer));
    ATX_OutputStream_WriteString(self->stream, buffer);
    ATX_OutputStream_WriteString(self->stream, ":");
    ATX_IntegerToStringU(record->timestamp.nanoseconds/1000000L, buffer, sizeof(buffer));
    ATX_OutputStream_WriteString(self->stream, buffer);
    ATX_OutputStream_Write(self->stream, " ", 1, NULL);
    if (self->use_colors) {
        ansi_color = ATX_Log_GetLogLevelAnsiColor(record->level);
        if (ansi_color) {
            ATX_OutputStream_Write(self->stream, "\033[", 2, NULL);
            ATX_OutputStream_WriteString(self->stream, ansi_color);
            ATX_OutputStream_Write(self->stream, ";1m", 3, NULL);
        }
    }
    ATX_OutputStream_WriteString(self->stream, level_name);
    if (self->use_colors && ansi_color) {
        ATX_OutputStream_Write(self->stream, "\033[0m", 4, NULL);
    }
    ATX_OutputStream_Write(self->stream, ": ", 2, NULL);
    ATX_OutputStream_WriteString(self->stream, record->message);
    ATX_OutputStream_Write(self->stream, "\r\n", 2, NULL);
}

/*----------------------------------------------------------------------
|   ATX_LogFileHandler_Destroy
+---------------------------------------------------------------------*/
static void
ATX_LogFileHandler_Destroy(ATX_LogHandler* _self)
{
    ATX_LogFileHandler* self = (ATX_LogFileHandler*)_self->instance;

    /* release the stream */
    ATX_RELEASE_OBJECT(self->stream);

    /* free the object memory */
    ATX_FreeMemory((void*)self);
}

/*----------------------------------------------------------------------
|   ATX_LogFileHandler_Create
+---------------------------------------------------------------------*/
static ATX_Result
ATX_LogFileHandler_Create(const char*     logger_name,
                          ATX_Boolean     use_console,
                          ATX_LogHandler* handler)
{
    ATX_LogFileHandler* instance;
    const char*         filename;
    ATX_String          filename_synth = ATX_EMPTY_STRING;
    ATX_File*           file;
    ATX_Result          result = ATX_SUCCESS;

    /* compute a prefix for the configuration of this handler */
    ATX_String logger_prefix = ATX_String_Create(logger_name);
    ATX_CHECK(ATX_String_Append(&logger_prefix, 
                                use_console ?
                                ".ConsoleHandler" :
                                ".FileHandler"));

    /* allocate a new object */
    instance = ATX_AllocateZeroMemory(sizeof(ATX_LogFileHandler));
    
    /* configure the object */
    if (use_console) {
        ATX_String* colors;
        filename = ATX_FILE_STANDARD_OUTPUT;
        instance->use_colors = ATX_LOG_CONSOLE_HANDLER_DEFAULT_COLOR_MODE;
        colors = ATX_LogManager_GetConfigValue(ATX_CSTR(logger_prefix),".colors");
        if (colors) {
            if (ATX_String_Compare(colors, "true", ATX_TRUE) == 0  ||
                ATX_String_Compare(colors, "yes", ATX_FALSE) == 0  ||
                ATX_String_Compare(colors, "on", ATX_FALSE) == 0) {
                instance->use_colors = ATX_TRUE;
            }
            if (ATX_String_Compare(colors, "false", ATX_TRUE) == 0  ||
                ATX_String_Compare(colors, "no", ATX_FALSE) == 0  ||
                ATX_String_Compare(colors, "off", ATX_FALSE) == 0) {
                instance->use_colors = ATX_FALSE;
            }
        }
    } else {
        ATX_String* filename_conf = ATX_LogManager_GetConfigValue(ATX_CSTR(logger_prefix), ".filename");
        if (filename_conf) {
            filename = ATX_CSTR(*filename_conf);
        } else if (logger_name[0]) {
            ATX_String_Reserve(&filename_synth, ATX_StringLength(logger_name));
            ATX_String_Assign(&filename_synth, logger_name);
            ATX_String_Append(&filename_synth, ".log");
            filename = ATX_CSTR(filename_synth);
        } else {
            /* default name for the root logger */
            filename = ATX_LOG_ROOT_DEFAULT_FILE_HANDLER_FILENAME;
        }
    }

    /* open the log file */
    if (ATX_SUCCEEDED(ATX_File_Create(filename, &file))) {
        result = ATX_File_Open(file, 
                                ATX_FILE_OPEN_MODE_CREATE   |
                                ATX_FILE_OPEN_MODE_TRUNCATE |
                                ATX_FILE_OPEN_MODE_WRITE);
        if (ATX_SUCCEEDED(result)) {
            result = ATX_File_GetOutputStream(file, &instance->stream);
            if (ATX_FAILED(result)) {
                instance->stream = NULL;
            }
        } else {
            ATX_Debug("ATX_LogFileHandler_Create - cannot open log file '%s' (%d)\n", filename, result);
        }
    
        ATX_DESTROY_OBJECT(file);
    }

    /* setup the interface */
    handler->instance = (ATX_LogHandlerInstance*)instance;
    handler->iface    = &ATX_LogFileHandler_Interface;

    /* cleanup */
    ATX_String_Destruct(&logger_prefix);
    ATX_String_Destruct(&filename_synth);

    return result;
}

/*----------------------------------------------------------------------
|   ATX_LogFileHandler_Interface
+---------------------------------------------------------------------*/
static const ATX_LogHandlerInterface 
ATX_LogFileHandler_Interface = {
    ATX_LogFileHandler_Log,
    ATX_LogFileHandler_Destroy
};

/*----------------------------------------------------------------------
|   ATX_LogTcpHandler forward references
+---------------------------------------------------------------------*/
static const ATX_LogHandlerInterface ATX_LogTcpHandler_Interface;

/*----------------------------------------------------------------------
|   ATX_LogTcpHandler_Connect
+---------------------------------------------------------------------*/
static ATX_Result
ATX_LogTcpHandler_Connect(ATX_LogTcpHandler* self)
{
    ATX_Result result = ATX_SUCCESS;

    /* create a socket */
    ATX_Socket* tcp_socket = NULL;
    ATX_CHECK(ATX_TcpClientSocket_Create(&tcp_socket));

    /* connect to the host */
    result = ATX_Socket_ConnectToHost(tcp_socket, ATX_CSTR(self->host), self->port, 
                                      ATX_LOG_TCP_HANDLER_DEFAULT_CONNECT_TIMEOUT);
    if (ATX_SUCCEEDED(result)) {
        /* get the stream */
        result = ATX_Socket_GetOutputStream(tcp_socket, &self->stream);
        if (ATX_FAILED(result)) self->stream = NULL;
    } else {
        ATX_Debug("ATX_LogTcpHandler_Connect - failed to connect to %s:%d (%d)\n",
                  ATX_CSTR(self->host), self->port, result);
    }

    /* cleanup */
    ATX_DESTROY_OBJECT(tcp_socket);

    return result;
}

/*----------------------------------------------------------------------
|   ATX_LogTcpHandler_Log
+---------------------------------------------------------------------*/
static void
ATX_LogTcpHandler_Log(ATX_LogHandler* _self, const ATX_LogRecord* record)
{
    ATX_LogTcpHandler* self = (ATX_LogTcpHandler*)_self->instance;

    /* ensure we're connected */
    if (self->stream == NULL) {
        if (ATX_FAILED(ATX_LogTcpHandler_Connect(self))) {
            return;
        }
    }

    {
        /* format the record */
        ATX_String  msg = ATX_EMPTY_STRING;
        const char* level_name = ATX_Log_GetLogLevelName(record->level);
        char        level_string[16];
        char        buffer[64];

        /* format and emit the record */
        if (level_name[0] == '\0') {
            ATX_IntegerToString(record->level, level_string, sizeof(level_string));
            level_name = level_string;
        }
        ATX_String_Reserve(&msg, 2048);
        ATX_String_Append(&msg, "Logger: ");
        ATX_String_Append(&msg, record->logger_name);
        ATX_String_Append(&msg, "\r\nLevel: ");
        ATX_String_Append(&msg, level_name);
        ATX_String_Append(&msg, "\r\nSource-File: ");
        ATX_String_Append(&msg, record->source_file);
        ATX_String_Append(&msg, "\r\nSource-Line: ");
        ATX_IntegerToStringU(record->source_line, buffer, sizeof(buffer));
        ATX_String_Append(&msg, buffer);
        ATX_String_Append(&msg, "\r\nTimeStamp: ");
        ATX_IntegerToStringU(record->timestamp.seconds, buffer, sizeof(buffer));
        ATX_String_Append(&msg, buffer);
        ATX_String_Append(&msg, ":");
        ATX_IntegerToStringU(record->timestamp.nanoseconds/1000000L, buffer, sizeof(buffer));
        ATX_String_Append(&msg, buffer);
        ATX_String_Append(&msg, "\r\nContent-Length: ");
        ATX_IntegerToString(ATX_StringLength(record->message), buffer, sizeof(buffer));
        ATX_String_Append(&msg, buffer);    
        ATX_String_Append(&msg, "\r\n\r\n");
        ATX_String_Append(&msg, record->message);

        /* emit the formatted record */
        if (ATX_FAILED(ATX_OutputStream_WriteString(self->stream, ATX_CSTR(msg)))) {
            ATX_RELEASE_OBJECT(self->stream);
        }

        ATX_String_Destruct(&msg);
    }
}

/*----------------------------------------------------------------------
|   ATX_LogTcpHandler_Destroy
+---------------------------------------------------------------------*/
static void
ATX_LogTcpHandler_Destroy(ATX_LogHandler* _self)
{
    ATX_LogTcpHandler* self = (ATX_LogTcpHandler*)_self->instance;

    /* destroy fields */
    ATX_String_Destruct(&self->host);

    /* release the stream */
    ATX_RELEASE_OBJECT(self->stream);

    /* free the object memory */
    ATX_FreeMemory((void*)self);
}

/*----------------------------------------------------------------------
|   ATX_LogTcpHandler_Create
+---------------------------------------------------------------------*/
static ATX_Result
ATX_LogTcpHandler_Create(const char* logger_name, ATX_LogHandler* handler)
{
    ATX_LogTcpHandler* instance;
    const ATX_String*  hostname;
    const ATX_String*  port;
    ATX_Result         result = ATX_SUCCESS;

    /* compute a prefix for the configuration of this handler */
    ATX_String logger_prefix = ATX_String_Create(logger_name);
    ATX_CHECK(ATX_String_Append(&logger_prefix, ".TcpHandler"));

    /* allocate a new object */
    instance = ATX_AllocateZeroMemory(sizeof(ATX_LogTcpHandler));

    /* configure the object */
    hostname = ATX_LogManager_GetConfigValue(ATX_CSTR(logger_prefix), ".hostname");
    if (hostname) {
        ATX_String_Assign(&instance->host, ATX_CSTR(*hostname));
    } else {
        /* default hostname */
        ATX_String_Assign(&instance->host, "localhost");
    }
    port = ATX_LogManager_GetConfigValue(ATX_CSTR(logger_prefix), ".port");
    if (port) {
        long port_int;
        if (ATX_SUCCEEDED(ATX_String_ToInteger(port, &port_int, ATX_TRUE))) {
            instance->port = (ATX_UInt16)port_int;
        } else {
            instance->port = ATX_LOG_TCP_HANDLER_DEFAULT_PORT;
        }
    } else {
        /* default port */
        instance->port = ATX_LOG_TCP_HANDLER_DEFAULT_PORT;
    }

    /* setup the interface */
    handler->instance = (ATX_LogHandlerInstance*)instance;
    handler->iface    = &ATX_LogTcpHandler_Interface;

    /* cleanup */
    ATX_String_Destruct(&logger_prefix);

    return result;
}

/*----------------------------------------------------------------------
|   ATX_LogTcpHandler_Interface
+---------------------------------------------------------------------*/
static const ATX_LogHandlerInterface 
ATX_LogTcpHandler_Interface = {
    ATX_LogTcpHandler_Log,
    ATX_LogTcpHandler_Destroy
};