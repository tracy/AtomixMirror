/*****************************************************************
|
|   Atomix - JSON
|
| Copyright (c) 2002-2010, Axiomatic Systems, LLC.
| All rights reserved.
|
| Redistribution and use in source and binary forms, with or without
| modification, are permitted provided that the following conditions are met:
|     * Redistributions of source code must retain the above copyright
|       notice, this list of conditions and the following disclaimer.
|     * Redistributions in binary form must reproduce the above copyright
|       notice, this list of conditions and the following disclaimer in the
|       documentation and/or other materials provided with the distribution.
|     * Neither the name of Axiomatic Systems nor the
|       names of its contributors may be used to endorse or promote products
|       derived from this software without specific prior written permission.
|
| THIS SOFTWARE IS PROVIDED BY AXIOMATIC SYSTEMS ''AS IS'' AND ANY
| EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
| WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
| DISCLAIMED. IN NO EVENT SHALL AXIOMATIC SYSTEMS BE LIABLE FOR ANY
| DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
| (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
| LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
| ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
| (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
| SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
|
 ****************************************************************/

/*----------------------------------------------------------------------
|    includes
+---------------------------------------------------------------------*/
#include "AtxJson.h"
#include "AtxDebug.h"

/*----------------------------------------------------------------------
|    types
+---------------------------------------------------------------------*/
typedef enum {
    ATX_JSON_PARSER_STATE_NAME,
    ATX_JSON_PARSER_STATE_VALUE,
    ATX_JSON_PARSER_STATE_NAMED_VALUE,
    ATX_JSON_PARSER_STATE_NUMBER,
    ATX_JSON_PARSER_STATE_STRING,
    ATX_JSON_PARSER_STATE_COLON,
    ATX_JSON_PARSER_STATE_DELIMITER,
    ATX_JSON_PARSER_STATE_LITERAL
} ATX_JsonParser_State;

struct ATX_Json {
    ATX_String   name;
    ATX_Json*    parent;
    ATX_Json*    child;
    ATX_Cardinal child_count;
    ATX_Json*    next;
    ATX_Json*    prev;
    ATX_JsonType type;
    union {
        double      number;
        ATX_String  string;
        ATX_Boolean boolean;
    } value;
};

typedef struct {
    ATX_JsonParser_State state;
    ATX_Boolean          in_escape;
    ATX_Boolean          in_unicode;
    ATX_Cardinal         unicode_chars;
    ATX_UInt32           unicode;
    ATX_String           name;
    ATX_String           value;
    ATX_Json*            context;
    ATX_Json*            root;
} ATX_JsonParser;

/*----------------------------------------------------------------------
|    constants
+---------------------------------------------------------------------*/
static ATX_Json ATX_Json_Null;

/*----------------------------------------------------------------------
|   character map (generated by MakeJsonCharMap.py)
|
| flags:
| 1  --> whitespace
| 2  --> digit
| 4  --> number char
| 8  --> literal
| 16 --> control
+---------------------------------------------------------------------*/
static const unsigned char ATX_JsonCharMap[256] = {
    /*   0 0x00     */       16,   /*   1 0x01     */       16,   /*   2 0x02     */       16,   /*   3 0x03     */       16,   
    /*   4 0x04     */       16,   /*   5 0x05     */       16,   /*   6 0x06     */       16,   /*   7 0x07     */       16,   
    /*   8 0x08     */       16,   /*   9 0x09     */     1|16,   /*  10 0x0a     */     1|16,   /*  11 0x0b     */       16,   
    /*  12 0x0c     */       16,   /*  13 0x0d     */     1|16,   /*  14 0x0e     */       16,   /*  15 0x0f     */       16,   
    /*  16 0x10     */       16,   /*  17 0x11     */       16,   /*  18 0x12     */       16,   /*  19 0x13     */       16,   
    /*  20 0x14     */       16,   /*  21 0x15     */       16,   /*  22 0x16     */       16,   /*  23 0x17     */       16,   
    /*  24 0x18     */       16,   /*  25 0x19     */       16,   /*  26 0x1a     */       16,   /*  27 0x1b     */       16,   
    /*  28 0x1c     */       16,   /*  29 0x1d     */       16,   /*  30 0x1e     */       16,   /*  31 0x1f     */       16,   
    /*  32 0x20 ' ' */        1,   /*  33 0x21 '!' */        0,   /*  34 0x22 '"' */        0,   /*  35 0x23 '#' */        0,   
    /*  36 0x24 '$' */        0,   /*  37 0x25 '%' */        0,   /*  38 0x26 '&' */        0,   /*  39 0x27 ''' */        0,   
    /*  40 0x28 '(' */        0,   /*  41 0x29 ')' */        0,   /*  42 0x2a '*' */        0,   /*  43 0x2b '+' */        4,   
    /*  44 0x2c ',' */        0,   /*  45 0x2d '-' */        4,   /*  46 0x2e '.' */        4,   /*  47 0x2f '/' */        0,   
    /*  48 0x30 '0' */      2|4,   /*  49 0x31 '1' */      2|4,   /*  50 0x32 '2' */      2|4,   /*  51 0x33 '3' */      2|4,   
    /*  52 0x34 '4' */      2|4,   /*  53 0x35 '5' */      2|4,   /*  54 0x36 '6' */      2|4,   /*  55 0x37 '7' */      2|4,   
    /*  56 0x38 '8' */      2|4,   /*  57 0x39 '9' */      2|4,   /*  58 0x3a ':' */        0,   /*  59 0x3b ';' */        0,   
    /*  60 0x3c '<' */        0,   /*  61 0x3d '=' */        0,   /*  62 0x3e '>' */        0,   /*  63 0x3f '?' */        0,   
    /*  64 0x40 '@' */        0,   /*  65 0x41 'A' */        0,   /*  66 0x42 'B' */        0,   /*  67 0x43 'C' */        0,   
    /*  68 0x44 'D' */        0,   /*  69 0x45 'E' */        4,   /*  70 0x46 'F' */        0,   /*  71 0x47 'G' */        0,   
    /*  72 0x48 'H' */        0,   /*  73 0x49 'I' */        0,   /*  74 0x4a 'J' */        0,   /*  75 0x4b 'K' */        0,   
    /*  76 0x4c 'L' */        0,   /*  77 0x4d 'M' */        0,   /*  78 0x4e 'N' */        0,   /*  79 0x4f 'O' */        0,   
    /*  80 0x50 'P' */        0,   /*  81 0x51 'Q' */        0,   /*  82 0x52 'R' */        0,   /*  83 0x53 'S' */        0,   
    /*  84 0x54 'T' */        0,   /*  85 0x55 'U' */        0,   /*  86 0x56 'V' */        0,   /*  87 0x57 'W' */        0,   
    /*  88 0x58 'X' */        0,   /*  89 0x59 'Y' */        0,   /*  90 0x5a 'Z' */        0,   /*  91 0x5b '[' */        0,   
    /*  92 0x5c '\' */        0,   /*  93 0x5d ']' */        0,   /*  94 0x5e '^' */        0,   /*  95 0x5f '_' */        0,   
    /*  96 0x60 '`' */        0,   /*  97 0x61 'a' */        8,   /*  98 0x62 'b' */        0,   /*  99 0x63 'c' */        0,   
    /* 100 0x64 'd' */        0,   /* 101 0x65 'e' */      4|8,   /* 102 0x66 'f' */        8,   /* 103 0x67 'g' */        0,   
    /* 104 0x68 'h' */        0,   /* 105 0x69 'i' */        0,   /* 106 0x6a 'j' */        0,   /* 107 0x6b 'k' */        0,   
    /* 108 0x6c 'l' */        8,   /* 109 0x6d 'm' */        0,   /* 110 0x6e 'n' */        8,   /* 111 0x6f 'o' */        0,   
    /* 112 0x70 'p' */        0,   /* 113 0x71 'q' */        0,   /* 114 0x72 'r' */        8,   /* 115 0x73 's' */        8,   
    /* 116 0x74 't' */        8,   /* 117 0x75 'u' */        8,   /* 118 0x76 'v' */        0,   /* 119 0x77 'w' */        0,   
    /* 120 0x78 'x' */        0,   /* 121 0x79 'y' */        0,   /* 122 0x7a 'z' */        0,   /* 123 0x7b '{' */        0,   
    /* 124 0x7c '|' */        0,   /* 125 0x7d '}' */        0,   /* 126 0x7e '~' */        0,   /* 127 0x7f     */        0,   
    /* 128 0x80     */        0,   /* 129 0x81     */        0,   /* 130 0x82     */        0,   /* 131 0x83     */        0,   
    /* 132 0x84     */        0,   /* 133 0x85     */        0,   /* 134 0x86     */        0,   /* 135 0x87     */        0,   
    /* 136 0x88     */        0,   /* 137 0x89     */        0,   /* 138 0x8a     */        0,   /* 139 0x8b     */        0,   
    /* 140 0x8c     */        0,   /* 141 0x8d     */        0,   /* 142 0x8e     */        0,   /* 143 0x8f     */        0,   
    /* 144 0x90     */        0,   /* 145 0x91     */        0,   /* 146 0x92     */        0,   /* 147 0x93     */        0,   
    /* 148 0x94     */        0,   /* 149 0x95     */        0,   /* 150 0x96     */        0,   /* 151 0x97     */        0,   
    /* 152 0x98     */        0,   /* 153 0x99     */        0,   /* 154 0x9a     */        0,   /* 155 0x9b     */        0,   
    /* 156 0x9c     */        0,   /* 157 0x9d     */        0,   /* 158 0x9e     */        0,   /* 159 0x9f     */        0,   
    /* 160 0xa0     */        0,   /* 161 0xa1     */        0,   /* 162 0xa2     */        0,   /* 163 0xa3     */        0,   
    /* 164 0xa4     */        0,   /* 165 0xa5     */        0,   /* 166 0xa6     */        0,   /* 167 0xa7     */        0,   
    /* 168 0xa8     */        0,   /* 169 0xa9     */        0,   /* 170 0xaa     */        0,   /* 171 0xab     */        0,   
    /* 172 0xac     */        0,   /* 173 0xad     */        0,   /* 174 0xae     */        0,   /* 175 0xaf     */        0,   
    /* 176 0xb0     */        0,   /* 177 0xb1     */        0,   /* 178 0xb2     */        0,   /* 179 0xb3     */        0,   
    /* 180 0xb4     */        0,   /* 181 0xb5     */        0,   /* 182 0xb6     */        0,   /* 183 0xb7     */        0,   
    /* 184 0xb8     */        0,   /* 185 0xb9     */        0,   /* 186 0xba     */        0,   /* 187 0xbb     */        0,   
    /* 188 0xbc     */        0,   /* 189 0xbd     */        0,   /* 190 0xbe     */        0,   /* 191 0xbf     */        0,   
    /* 192 0xc0     */        0,   /* 193 0xc1     */        0,   /* 194 0xc2     */        0,   /* 195 0xc3     */        0,   
    /* 196 0xc4     */        0,   /* 197 0xc5     */        0,   /* 198 0xc6     */        0,   /* 199 0xc7     */        0,   
    /* 200 0xc8     */        0,   /* 201 0xc9     */        0,   /* 202 0xca     */        0,   /* 203 0xcb     */        0,   
    /* 204 0xcc     */        0,   /* 205 0xcd     */        0,   /* 206 0xce     */        0,   /* 207 0xcf     */        0,   
    /* 208 0xd0     */        0,   /* 209 0xd1     */        0,   /* 210 0xd2     */        0,   /* 211 0xd3     */        0,   
    /* 212 0xd4     */        0,   /* 213 0xd5     */        0,   /* 214 0xd6     */        0,   /* 215 0xd7     */        0,   
    /* 216 0xd8     */        0,   /* 217 0xd9     */        0,   /* 218 0xda     */        0,   /* 219 0xdb     */        0,   
    /* 220 0xdc     */        0,   /* 221 0xdd     */        0,   /* 222 0xde     */        0,   /* 223 0xdf     */        0,   
    /* 224 0xe0     */        0,   /* 225 0xe1     */        0,   /* 226 0xe2     */        0,   /* 227 0xe3     */        0,   
    /* 228 0xe4     */        0,   /* 229 0xe5     */        0,   /* 230 0xe6     */        0,   /* 231 0xe7     */        0,   
    /* 232 0xe8     */        0,   /* 233 0xe9     */        0,   /* 234 0xea     */        0,   /* 235 0xeb     */        0,   
    /* 236 0xec     */        0,   /* 237 0xed     */        0,   /* 238 0xee     */        0,   /* 239 0xef     */        0,   
    /* 240 0xf0     */        0,   /* 241 0xf1     */        0,   /* 242 0xf2     */        0,   /* 243 0xf3     */        0,   
    /* 244 0xf4     */        0,   /* 245 0xf5     */        0,   /* 246 0xf6     */        0,   /* 247 0xf7     */        0,   
    /* 248 0xf8     */        0,   /* 249 0xf9     */        0,   /* 250 0xfa     */        0,   /* 251 0xfb     */        0,   
    /* 252 0xfc     */        0,   /* 253 0xfd     */        0,   /* 254 0xfe     */        0,   /* 255 0xff     */        0   
};

#define ATX_JSON_CHAR_IS_WHITESPACE(c) (ATX_JsonCharMap[c]&1)
#define ATX_JSON_CHAR_IS_DIGIT(c)      (ATX_JsonCharMap[c]&2)
#define ATX_JSON_CHAR_IS_NUMBER(c)     (ATX_JsonCharMap[c]&4)
#define ATX_JSON_CHAR_IS_LITERAL(c)    (ATX_JsonCharMap[c]&8)
#define ATX_JSON_CHAR_IS_CONTROL(c)    (ATX_JsonCharMap[c]&16)

/*----------------------------------------------------------------------
|    ATX_Json_Create
+---------------------------------------------------------------------*/
static ATX_Json* 
ATX_Json_Create(ATX_JsonType type)
{
    ATX_Json* json = ATX_AllocateZeroMemory(sizeof(ATX_Json));
    if (json == NULL) return NULL;
    json->type = type;
    
    return json;
}

/*----------------------------------------------------------------------
|    ATX_Json_CreateArray
+---------------------------------------------------------------------*/
ATX_Json* 
ATX_Json_CreateArray(void)
{
    return ATX_Json_Create(ATX_JSON_TYPE_ARRAY);
}

/*----------------------------------------------------------------------
|    ATX_Json_CreateObject
+---------------------------------------------------------------------*/
ATX_Json* 
ATX_Json_CreateObject(void)
{
    return ATX_Json_Create(ATX_JSON_TYPE_OBJECT);
}

/*----------------------------------------------------------------------
|    ATX_Json_CreateString
+---------------------------------------------------------------------*/
ATX_Json* 
ATX_Json_CreateString(const char* value)
{
    ATX_Json* json = ATX_Json_Create(ATX_JSON_TYPE_STRING);
    if (json == NULL) return NULL;
    ATX_String_Assign(&json->value.string, value);

    return json;
}

/*----------------------------------------------------------------------
|    ATX_Json_CreateNumber
+---------------------------------------------------------------------*/
ATX_Json* 
ATX_Json_CreateNumber(double number)
{
    ATX_Json* json = ATX_Json_Create(ATX_JSON_TYPE_NUMBER);
    if (json == NULL) return NULL;
    json->value.number = number;

    return json;
}

/*----------------------------------------------------------------------
|    ATX_Json_CreateBoolean
+---------------------------------------------------------------------*/
ATX_Json* 
ATX_Json_CreateBoolean(ATX_Boolean value)
{
    ATX_Json* json = ATX_Json_Create(ATX_JSON_TYPE_BOOLEAN);
    if (json == NULL) return NULL;
    json->value.boolean = value;

    return json;
}

/*----------------------------------------------------------------------
|    ATX_Json_CreateNull
+---------------------------------------------------------------------*/
ATX_Json* 
ATX_Json_CreateNull(void)
{
    return ATX_Json_Create(ATX_JSON_TYPE_NULL);
}

/*----------------------------------------------------------------------
|    ATX_Json_Destroy
+---------------------------------------------------------------------*/
void
ATX_Json_Destroy(ATX_Json* self)
{
    ATX_Json* child = self->child;
    if (self->type == ATX_JSON_TYPE_STRING) {
        ATX_String_Destruct(&self->value.string);
    }
    while (child) {
        ATX_Json* next = child->next;
        ATX_Json_Destroy(child);
        child = next;
    }
    ATX_String_Destruct(&self->name);
    ATX_FreeMemory(self);
}

/*----------------------------------------------------------------------
|    ATX_Json_GetChild
+---------------------------------------------------------------------*/
ATX_Json*    
ATX_Json_GetChild(ATX_Json* self, const char* name)
{
    ATX_Json* child = self->child;
    while (child) {
        if (ATX_String_Equals(&child->name, name, ATX_FALSE)) {
            return child;
        }
        child = child->next;
    }
    
    return NULL;
}

/*----------------------------------------------------------------------
|    ATX_Json_GetChildAt
+---------------------------------------------------------------------*/
ATX_Json*    
ATX_Json_GetChildAt(ATX_Json* self, ATX_Ordinal indx, const char** name)
{
    ATX_Json* child = self->child;
    if (name) *name = NULL;
    
    /* start from the end of the list */
    if (indx >= self->child_count) return NULL;
    indx = self->child_count-indx-1;
    while (indx) {
        child = child->next;
        if (child == NULL) return NULL;
        --indx;
    }
    if (name) *name = ATX_String_GetChars(&child->name);
    return child;
}

/*----------------------------------------------------------------------
|    ATX_Json_GetChildCount
+---------------------------------------------------------------------*/
ATX_Cardinal
ATX_Json_GetChildCount(ATX_Json* self)
{
    return self->child_count;
}

/*----------------------------------------------------------------------
|    ATX_Json_GetParent
+---------------------------------------------------------------------*/
ATX_Json*         
ATX_Json_GetParent(ATX_Json* self)
{
    return self->parent;
}

/*----------------------------------------------------------------------
|    ATX_Json_AddChild
+---------------------------------------------------------------------*/
ATX_Result  
ATX_Json_AddChild(ATX_Json* self, const char* name, ATX_Json* child)
{
    /* check that we can add a child to this object */
    if (self->type == ATX_JSON_TYPE_ARRAY) {
        if (!(name == NULL || name[0] == '\0')) {
            return ATX_ERROR_INVALID_PARAMETERS;
        }
    } else if (self->type != ATX_JSON_TYPE_OBJECT) {
        return ATX_ERROR_INVALID_PARAMETERS;
    }
    
    ATX_String_Assign(&child->name, name);
    child->parent     = self;
    child->next       = self->child;
    child->prev       = NULL;
    if (self->child) self->child->prev = child;
    self->child       = child;
    ++self->child_count;
    
    return ATX_SUCCESS; 
}

/*----------------------------------------------------------------------
|    ATX_Json_GetType
+---------------------------------------------------------------------*/
ATX_JsonType 
ATX_Json_GetType(ATX_Json* self)
{
    return self->type;
}

/*----------------------------------------------------------------------
|    ATX_Json_AsInteger
+---------------------------------------------------------------------*/
ATX_Int32    
ATX_Json_AsInteger(ATX_Json* self)
{
    if (self->type == ATX_JSON_TYPE_NUMBER) {
        return (ATX_Int32)self->value.number;
    } else {
        return 0;
    }
}

/*----------------------------------------------------------------------
|    ATX_Json_AsDouble
+---------------------------------------------------------------------*/
double       
ATX_Json_AsDouble(ATX_Json* self)
{
    if (self->type == ATX_JSON_TYPE_NUMBER) {
        return self->value.number;
    } else {
        return 0.0;
    }
}

/*----------------------------------------------------------------------
|    ATX_Json_AsBoolean
+---------------------------------------------------------------------*/
ATX_Boolean  
ATX_Json_AsBoolean(ATX_Json* self)
{
    if (self->type == ATX_JSON_TYPE_BOOLEAN) {
        return self->value.boolean;
    } else {
        return ATX_FALSE;
    }
}

/*----------------------------------------------------------------------
|    ATX_Json_AsString
+---------------------------------------------------------------------*/
const ATX_String*  
ATX_Json_AsString(ATX_Json* self)
{
    if (self->type == ATX_JSON_TYPE_STRING) {
        return &self->value.string;
    } else {
        return &ATX_Json_Null.value.string;
    }
}

/*----------------------------------------------------------------------
|   ATX_JsonParser_Construct
+---------------------------------------------------------------------*/
static void
ATX_JsonParser_Construct(ATX_JsonParser* self)
{
    self->state         = ATX_JSON_PARSER_STATE_VALUE;
    self->in_escape     = ATX_FALSE;
    self->in_unicode    = ATX_FALSE;
    self->unicode_chars = 0;
    self->unicode       = 0;
    self->context       = NULL;
    self->root          = NULL;
    ATX_String_Construct(&self->name);
    ATX_String_Construct(&self->value);
}

/*----------------------------------------------------------------------
|   ATX_JsonParser_Destruct
+---------------------------------------------------------------------*/
static void
ATX_JsonParser_Destruct(ATX_JsonParser* self)
{
    if (self->context) ATX_Json_Destroy(self->context);
    ATX_String_Destruct(&self->name);
    ATX_String_Destruct(&self->value);
}

/*----------------------------------------------------------------------
|   ATX_JsonParser_OnNewValue
+---------------------------------------------------------------------*/
static void
ATX_JsonParser_OnNewValue(ATX_JsonParser* self, ATX_Json* value)
{
    if (self->context) {
        const char* name = NULL;
        if (!ATX_String_IsEmpty(&self->name)) {
            name = ATX_String_GetChars(&self->name);
        }
        ATX_Json_AddChild(self->context, name, value);
        if (name) ATX_String_SetLength(&self->name, 0);
    } else {
        ATX_ASSERT(self->root == NULL);
        self->root = value;
    }
    
    /* reset the value buffer */
    ATX_String_SetLength(&self->value, 0);
}

/*----------------------------------------------------------------------
|   ATX_JsonParser_AppendUTF8
+---------------------------------------------------------------------*/
static void
ATX_JsonParser_AppendUTF8(ATX_String* dest, unsigned int c)
{
    if (c <= 0x7F) {
        /* 000000-00007F -> 1 char = 0xxxxxxx */
        ATX_String_AppendChar(dest,(char)c);
    } else if (c <= 0x7FF) {
        /* 000080-0007FF -> 2 chars = 110zzzzx 10xxxxxx */
        ATX_String_AppendChar(dest, 0xC0|(c>>6));
        ATX_String_AppendChar(dest, 0x80|(c&0x3F));
    } else if (c <= 0xFFFF) {
        /* 000800-00FFFF -> 3 chars = 1110zzzz 10zxxxxx 10xxxxxx */
        ATX_String_AppendChar(dest, 0xE0| (c>>12      ));
        ATX_String_AppendChar(dest, 0x80|((c&0xFC0)>>6));
        ATX_String_AppendChar(dest, 0x80| (c&0x3F     ));
    } else if (c <= 0x10FFFF) {
        /* 010000-10FFFF -> 4 chars = 11110zzz 10zzxxxx 10xxxxxx 10xxxxxx */
        ATX_String_AppendChar(dest, 0xF0| (c>>18         ));
        ATX_String_AppendChar(dest, 0x80|((c&0x3F000)>>12));
        ATX_String_AppendChar(dest, 0x80|((c&0xFC0  )>> 6));
        ATX_String_AppendChar(dest, 0x80| (c&0x3F        ));
    }
}

/*----------------------------------------------------------------------
|   ATX_JsonParser_Parse
+---------------------------------------------------------------------*/
static ATX_Result   
ATX_JsonParser_Parse(ATX_JsonParser* self, const char* serialized, ATX_Size size)
{
    /* parse chars one by one */
    while (size) {
        unsigned char c = *serialized;
        switch (self->state) {
          case ATX_JSON_PARSER_STATE_VALUE:
            if (ATX_JSON_CHAR_IS_WHITESPACE(c)) break;
            if (c == '\0') break;
            if (c == '{') {
                ATX_Json* object = ATX_Json_CreateObject();
                ATX_JsonParser_OnNewValue(self, object);
                self->context = object;
                self->state = ATX_JSON_PARSER_STATE_NAMED_VALUE;
            } else if (c == '[') {
                ATX_Json* array = ATX_Json_CreateArray();
                ATX_JsonParser_OnNewValue(self, array);
                self->context = array;
                self->state = ATX_JSON_PARSER_STATE_VALUE;
            } else if (c == ']') {
                if (self->context == NULL || self->context->child_count) {
                    return ATX_ERROR_INVALID_SYNTAX;
                }
                self->state = ATX_JSON_PARSER_STATE_DELIMITER;
                continue;
            } else if (c == '-' || ATX_JSON_CHAR_IS_DIGIT(c)) {
                self->state = ATX_JSON_PARSER_STATE_NUMBER;
                continue;
            } else if (c == '"') {
                self->state = ATX_JSON_PARSER_STATE_STRING;
            } else if (ATX_JSON_CHAR_IS_LITERAL(c)) {
                self->state = ATX_JSON_PARSER_STATE_LITERAL;
                continue;
            } else {
                return ATX_ERROR_INVALID_SYNTAX;
            }
            break;
            
          case ATX_JSON_PARSER_STATE_NAMED_VALUE:
            if (ATX_JSON_CHAR_IS_WHITESPACE(c)) break;
            if (c == '"') {
                self->state = ATX_JSON_PARSER_STATE_NAME;
            } else if (c == '}') {
                if (self->context == NULL || self->context->child_count) {
                    return ATX_ERROR_INVALID_SYNTAX;
                }
                self->state = ATX_JSON_PARSER_STATE_DELIMITER;
                continue;
            } else {
                return ATX_ERROR_INVALID_SYNTAX;
            }
            break;
            
          case ATX_JSON_PARSER_STATE_DELIMITER:
            if (ATX_JSON_CHAR_IS_WHITESPACE(c)) break;
            if (self->context == NULL) {
                if (c == '\0') {
                    break;
                } else {
                    return ATX_ERROR_INVALID_SYNTAX;
                }
            }
            if ((c == '}' && self->context->type == ATX_JSON_TYPE_OBJECT) ||
                (c == ']' && self->context->type == ATX_JSON_TYPE_ARRAY)) {
                self->context = self->context->parent;
                break;
            }
            if (c != ',') return ATX_ERROR_INVALID_SYNTAX;
            if (self->context->type == ATX_JSON_TYPE_OBJECT) {
                self->state = ATX_JSON_PARSER_STATE_NAMED_VALUE;
            } else {
                self->state = ATX_JSON_PARSER_STATE_VALUE;
            }
            break;
            
          case ATX_JSON_PARSER_STATE_COLON:
            if (ATX_JSON_CHAR_IS_WHITESPACE(c)) break;
            if (c == ':') {
                self->state = ATX_JSON_PARSER_STATE_VALUE;
            } else {
                return ATX_ERROR_INVALID_SYNTAX;
            }
            break;
            
          case ATX_JSON_PARSER_STATE_NAME:
          case ATX_JSON_PARSER_STATE_STRING:
            if (self->in_unicode) {
                int nibble = ATX_HexToNibble(c);
                if (nibble < 0) return ATX_ERROR_INVALID_SYNTAX;
                self->unicode = (self->unicode<<4) | nibble;
                if (++self->unicode_chars == 4) {
                    if (self->state == ATX_JSON_PARSER_STATE_NAME) {
                        ATX_JsonParser_AppendUTF8(&self->name, self->unicode);
                    } else {
                        ATX_JsonParser_AppendUTF8(&self->value, self->unicode);
                    }
                    self->in_unicode = ATX_FALSE;
                    self->unicode_chars = 0;
                    self->unicode = 0;
                }
                break;
            } else if (self->in_escape) {
                self->in_escape = ATX_FALSE;
                switch (c) {
                  case '"':
                  case '\\': 
                  case '/': break;                  
                  case 'b': c = '\b'; break;
                  case 'f': c = '\f'; break;
                  case 'n': c = '\n'; break;
                  case 'r': c = '\r'; break;
                  case 't': c = '\t'; break;
                  case 'u': 
                    self->in_unicode = ATX_TRUE; 
                    break;
                  default: return ATX_ERROR_INVALID_SYNTAX;
                }
                if (self->in_unicode) break;
                if (self->state == ATX_JSON_PARSER_STATE_NAME) {
                    ATX_JsonParser_AppendUTF8(&self->name, c);
                } else {
                    ATX_JsonParser_AppendUTF8(&self->value, c);
                }
                break;
            }
            if (c == '"') {
                if (self->state == ATX_JSON_PARSER_STATE_NAME) {
                    self->state = ATX_JSON_PARSER_STATE_COLON;
                } else {
                    ATX_JsonParser_OnNewValue(self, ATX_Json_CreateString(ATX_CSTR(self->value)));
                    self->state = ATX_JSON_PARSER_STATE_DELIMITER;
                }
                break;
            }
            if (c == '\\') {
                self->in_escape = ATX_TRUE;
                break;
            } else if (c == '\0' || ATX_JSON_CHAR_IS_CONTROL(c)) {
                return ATX_ERROR_INVALID_SYNTAX;
            }
            if (self->state == ATX_JSON_PARSER_STATE_NAME) {
                ATX_String_AppendChar(&self->name, c);
            } else {
                ATX_String_AppendChar(&self->value, c);
            }
            break;
          
          case ATX_JSON_PARSER_STATE_NUMBER:
            if (ATX_JSON_CHAR_IS_NUMBER(c)) {
                ATX_String_AppendChar(&self->value, c);
            } else {
                double     number = 0.0;
                ATX_Result result;
                
                /* integers can't start with a zero */
                if (ATX_String_GetLength(&self->value) >= 2) {
                    const char* n = ATX_CSTR(self->value);
                    if (n[0] == '-') ++n;
                    if (n[0] == '0' && !(n[1] == '.' || n[1] == '\0')) {
                        return ATX_ERROR_INVALID_SYNTAX;
                    }
                }
                
                /* parse the number */
                result = ATX_ParseDouble(ATX_CSTR(self->value), &number, ATX_FALSE);
                if (ATX_FAILED(result)) return ATX_ERROR_INVALID_SYNTAX;
                ATX_JsonParser_OnNewValue(self, ATX_Json_CreateNumber(number));
                self->state = ATX_JSON_PARSER_STATE_DELIMITER;
                continue;
            }
            break;

          case ATX_JSON_PARSER_STATE_LITERAL:
            if (ATX_JSON_CHAR_IS_LITERAL(c)) {
                ATX_String_AppendChar(&self->value, c);
            } else {
                if (ATX_String_Equals(&self->value, "true", ATX_FALSE)) {
                    ATX_JsonParser_OnNewValue(self, ATX_Json_CreateBoolean(ATX_TRUE));
                } else if (ATX_String_Equals(&self->value, "false", ATX_FALSE)) {
                    ATX_JsonParser_OnNewValue(self, ATX_Json_CreateBoolean(ATX_FALSE));
                } else if (ATX_String_Equals(&self->value, "null", ATX_FALSE)) {
                    ATX_JsonParser_OnNewValue(self, ATX_Json_CreateNull());
                } else {
                    return ATX_ERROR_INVALID_SYNTAX;
                }
                self->state = ATX_JSON_PARSER_STATE_DELIMITER;
                continue;
            } 
            break;
        }
        ++serialized;
        --size;
    }
    
    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   ATX_Json_ParseBuffer
+---------------------------------------------------------------------*/
ATX_Result   
ATX_Json_ParseBuffer(const char* serialized, ATX_Size size, ATX_Json** json)
{
    ATX_JsonParser parser;
    ATX_Result     result;
    char           termination = '\0';
    
    /* construct the parser */
    ATX_JsonParser_Construct(&parser);
    
    /* start empty */
    *json = NULL;

    /* parse the buffer */
    result = ATX_JsonParser_Parse(&parser, serialized, size);
    if (ATX_FAILED(result)) goto end;
    
    /* feed a termination */
    result = ATX_JsonParser_Parse(&parser, &termination, 1);
    if (ATX_FAILED(result)) goto end;

    /* return the root object produced by the parser */
    if (ATX_SUCCEEDED(result)) {
        if (parser.context) {
            result = ATX_ERROR_INVALID_SYNTAX;
        } else {
            *json = parser.root;
            parser.root = NULL;
        }
    }
    
end:
    /* destruct the parser */
    ATX_JsonParser_Destruct(&parser);
    
    return result;
}

/*----------------------------------------------------------------------
|    ATX_Json_Parse
+---------------------------------------------------------------------*/
ATX_Result   
ATX_Json_Parse(const char* serialized, ATX_Json** json)
{
    return ATX_Json_ParseBuffer(serialized, ATX_StringLength(serialized), json);
}

/*----------------------------------------------------------------------
|    ATX_Json_EmitString
+---------------------------------------------------------------------*/
static void
ATX_Json_EmitString(ATX_String* string, ATX_String* buffer)
{
    const char* s = ATX_CSTR(*string);
    char        c;
    
    ATX_String_AppendChar(buffer, '"');
    while ((c = *s++)) {
        char escape = '\0';
        switch (c) {
          case '"':  escape = '"';  break;
          case '\\': escape = '\\'; break;
          case '\b': escape = 'b';  break;
          case '\f': escape = 'f';  break;
          case '\n': escape = 'n';  break;
          case '\r': escape = 'r';  break;
          case '\t': escape = 't';  break;
        }
        if (escape) {
            ATX_String_AppendChar(buffer, '\\');
            ATX_String_AppendChar(buffer, escape);
        } else {
            ATX_String_AppendChar(buffer, c);
        }
    }
    ATX_String_AppendChar(buffer, '"');
}

/*----------------------------------------------------------------------
|    ATX_Json_Emit
+---------------------------------------------------------------------*/
static ATX_Result   
ATX_Json_Emit(ATX_Json*   self, 
              ATX_String* prefix, 
              ATX_String* buffer, 
              ATX_Boolean in_object,
              ATX_Boolean pretty)
{
    char      workspace[256];
    ATX_Json* child;
    
    if (pretty) ATX_String_Append(buffer, ATX_CSTR(*prefix));
    if (in_object) {
        ATX_Json_EmitString(&self->name, buffer);
        ATX_String_Append(buffer, ": ");
    }
    switch (self->type) {
      case ATX_JSON_TYPE_NUMBER:
        if (self->value.number == (ATX_Int32)self->value.number) {
            ATX_IntegerToString((ATX_Int32)self->value.number, workspace, sizeof(workspace));
        } else {
            double abs_val = self->value.number>=0.0?self->value.number:-self->value.number;
            if (abs_val < 1E-6 || abs_val > 1E9) {
                ATX_FormatStringN(workspace, sizeof(workspace), "%e", (float)self->value.number); 
            } else {
                ATX_FormatStringN(workspace, sizeof(workspace), "%f", (float)self->value.number); 
            }
        }
        ATX_String_Append(buffer, workspace);
        break;
        
      case ATX_JSON_TYPE_STRING:
        ATX_Json_EmitString(&self->value.string, buffer);
        break;
        
      case ATX_JSON_TYPE_BOOLEAN:
        ATX_String_Append(buffer, self->value.boolean?"true":"false");
        break;
        
      case ATX_JSON_TYPE_NULL:
        ATX_String_Append(buffer, "null");
        break;

      case ATX_JSON_TYPE_ARRAY:
        if (self->child_count == 0) {
            ATX_String_Append(buffer, "[]");
            break;
        }
        ATX_String_AppendChar(buffer, '[');
        if (pretty) {
            ATX_String_Append(prefix, "    ");
            ATX_String_Append(buffer, "\n");
        }
        child = self->child;
        while (child && child->next) child = child->next;
        while (child) {
            ATX_Json_Emit(child, prefix, buffer, ATX_FALSE, pretty);
            child = child->prev;
            if (child) {
                ATX_String_Append(buffer, pretty?",\n":", ");
            } else {
                if (pretty) ATX_String_Append(buffer, "\n");
            }
        }
        if (pretty) {
            ATX_String_SetLength(prefix, ATX_String_GetLength(prefix)-4);
            ATX_String_Append(buffer, ATX_CSTR(*prefix));
        }
        ATX_String_AppendChar(buffer, ']');
        break;

      case ATX_JSON_TYPE_OBJECT:
        if (self->child_count == 0) {
            ATX_String_Append(buffer, "{}");
            break;
        }
        ATX_String_AppendChar(buffer, '{');
        if (pretty) {
            ATX_String_Append(prefix, "    ");
            ATX_String_Append(buffer, "\n");
        }
        child = self->child;
        while (child && child->next) child = child->next;
        while (child) {
            ATX_Json_Emit(child, prefix, buffer, ATX_TRUE, pretty);
            child = child->prev;
            if (child) {
                ATX_String_Append(buffer, pretty?",\n":", ");
            } else {
                if (pretty) ATX_String_Append(buffer, "\n");
            }
        }
        if (pretty) {
            ATX_String_SetLength(prefix, ATX_String_GetLength(prefix)-4);
            ATX_String_Append(buffer, ATX_CSTR(*prefix));
        }
        ATX_String_AppendChar(buffer, '}');
        break;
    }

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|    ATX_Json_Serialize
+---------------------------------------------------------------------*/
ATX_Result   
ATX_Json_Serialize(ATX_Json* self, ATX_String* buffer, ATX_Boolean pretty)
{
    ATX_String prefix = ATX_EMPTY_STRING;
    ATX_Result result;
    
    ATX_String_SetLength(buffer, 0);

    result = ATX_Json_Emit(self, &prefix, buffer, ATX_FALSE, pretty);
    ATX_String_Destruct(&prefix);
    
    return result;
}
