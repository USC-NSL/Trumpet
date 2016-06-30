#ifndef MESSAGES_H
#define MESSAGES_H 1
#include <inttypes.h>
#include "flow.h"

#define MESSAGE_BUFSIZE 8

enum messagetype{
        mt_hello,
        mt_addtrigger,
        mt_addtrigger_return,
        mt_deltrigger,
        mt_deltrigger_return,
        mt_triggersatisfaction,
        mt_triggerquery,
        mt_triggerquery_return,
        mt_bye
};


struct messageheader{
        enum messagetype type;
        uint16_t length;
};

struct message_hello{
        uint32_t id;
        uint32_t time;
};

struct message_addtrigger{
        struct flow f;
        uint32_t eventid;
        struct flow mask;
        char buf[MESSAGE_BUFSIZE];
        //uint32_t type_id;
};

struct message_deltrigger{
        struct flow f;
        uint32_t eventid;
        struct flow mask;
};

struct message_deltrigger_return{
        uint32_t time;
        uint16_t eventid;
        bool success;
};


struct message_addtrigger_return{
        uint32_t time;
        uint16_t eventid;
        bool success;
};

struct message_triggersatisfaction{
        uint32_t time;
        uint16_t eventid;
        uint16_t code;
        char buf [MESSAGE_BUFSIZE];
};

struct message_triggerquery{
        struct flow f;
        uint16_t eventid;
        uint16_t time;
        struct flow mask;
};

#endif /* messages.h */
