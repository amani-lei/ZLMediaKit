# 图像质量检测


#### 请求


    GET&&POST
    /index/api/quality_analysis

|参数名|必须的|类型|描述|
|---|---|---|---|
|m|是|install | uninstall|安装或写在分析任务|
|request_id|是|字符串|请求开启质量分析的唯一请求id|
|---|---|---|当m为install时, 下面参数是必须的|
|stream_id|是|字符串|需要分析的流id|
|result_hook|是|url, 需要做url编码|一个回调地址(post), 分析结果会通过该回调发送|

#### 应答

    应答成功仅代表接受本次检测请求

```json
{
    "code":0,
    "msg":"success"
}
```

#### 结果回调

    POST

|参数名|必须的|类型|描述|
|---|---|---|---|
|code|是|number|分析结果|
|msg|是|string|请求结果的描述|
|request_id|是|string|请求分析时, 调用接口携带的请求id|
|request_time|是|number|检测时的毫秒时间戳|
|stream_id|是|number|分析结果由哪个流产生的|
|response_time|是|number|应答时间戳|
|loss_pkt_rate_total|是|float|总丢包率0-1.0|
|loss_pkt_rate_minu1|是|float|最近1分钟的丢包率0-1.0|

|block_detect|是|float|色块检测|
|brightness_detect|是|float|亮度检测|
|snow_noise_detect|是|float|雪花检测|
|sharpness_detect|是|float|清晰度检测|

```json
{
    "code": 0,
    "msg": "success",
    "request_id": "xxxx",
    "request_time":"16099900",
    "stream_id":"xxxxxxxxx",
    "response_time":"16810000",
    "loss_pkt_rate_total":"0.24",
    "loss_pkt_rate_minu1":"0.03",
    "block_detect":"32.67",
    "brightness_detect":"0.53",
    "snow_noise_detect":"0.24",
    "sharpness_detect":"354.09"
}
```


