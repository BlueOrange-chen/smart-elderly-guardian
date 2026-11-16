#ifndef _ALIOT_DM_H_
#define _ALIOT_DM_H_
#include "cJSON.h"

typedef struct aliot_dm
{
    cJSON* dm_js;
    char* dm_js_str;
}ALIOT_DM_DES;

//生成一个物模型描述结构体
ALIOT_DM_DES *aliot_malloc_des(void);

//往物模型描述结构体添加属性值
void aliot_set_dm_int(ALIOT_DM_DES *dm,const char* name,int value);

//生成CJSON字符串，保存在dm_js_str
void aliot_dm_serialize(ALIOT_DM_DES *dm);

//释放物模型描述结构体
void aliot_dm_free(ALIOT_DM_DES *dm);


#endif
