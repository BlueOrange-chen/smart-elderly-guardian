#include "aliot_dm.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static int s_aliot_id = 0;

//生成一个物模型描述结构体
ALIOT_DM_DES *aliot_malloc_des(void)
{
    ALIOT_DM_DES *dm = (ALIOT_DM_DES*)malloc(sizeof(ALIOT_DM_DES));     //malloc是动态内存分配函数
    if(dm)
    {
        memset(dm,0,sizeof(ALIOT_DM_DES));  
        dm->dm_js = cJSON_CreateObject();       //保存物模型cJSON对象
        char id[10];
        snprintf(id,sizeof(id),"%d",s_aliot_id++);        //格式化id
        cJSON_AddStringToObject(dm->dm_js,"id",id);                 //这些需要填写的内容可以在阿里云使用手册里面找到
        cJSON_AddStringToObject(dm->dm_js,"version","1.0");
        cJSON_AddStringToObject(dm->dm_js,"method","thing.event.property.post");
        cJSON_AddObjectToObject(dm->dm_js,"params");
        return dm;
    }
    return NULL;
}

//往物模型描述结构体添加属性值
void aliot_set_dm_int(ALIOT_DM_DES *dm,const char* name,int value)
{
    if (dm)
    {
        cJSON *params_js = cJSON_GetObjectItem(dm->dm_js,"params");   //取得其中一个对象
        if(params_js)
        {
            cJSON_AddNumberToObject(params_js,name,value);
        }
    }
}

//生成CJSON字符串，保存在dm_js_str
void aliot_dm_serialize(ALIOT_DM_DES *dm)
{
    if(dm)
    {
        if(dm->dm_js_str)   //校验cJSON字符串是否有效
        {
            cJSON_free(dm->dm_js_str);  //无效的话释放
            dm->dm_js_str = NULL;       //重置cJSON字符串
        }
        dm->dm_js_str = cJSON_PrintUnformatted(dm->dm_js);//生成JSON格式字符串
    }
}

//释放物模型描述结构体
void aliot_dm_free(ALIOT_DM_DES *dm)
{
    if(dm)
    {
        if(dm->dm_js_str)
        {
            cJSON_free(dm->dm_js_str);
            dm->dm_js_str = NULL;
        }
        if(dm->dm_js)
        {
            cJSON_Delete(dm->dm_js);
            dm->dm_js = NULL;
        }
        free(dm);
    }
}