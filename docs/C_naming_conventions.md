# C naming Conventions

## varible

 - b:bool
 - p:pointer
 - f:float
 - d:double
 - c:char
 - s:string
 - st:struct
 - u:union
 - e:enum
 - u8:U8
 - u32:U32
 - ul:UL
 - s8:S8
 - s32:S32
 - sl:SL

### local varible

```
S32 total_num;
S32 tmp;
S32 i;
```

### global varible

```
S32 s32TotalNum;
U8  *pDataPointer;
```

### struct

```
typedef struct _ALSoftOpenh264EncContext ALSoftOpenh264EncContext;

struct _ALSoftOpenh264EncContext {
    ALBaseContext stAlBaseContext;
    ISVCEncoder *pstSvcEncoder;
    SEncParamBase stParam;
    BOOL bResult;
};
```

## function

### internal function

```
void parse_argument();
```

### external function

```
S32 al_request_output_frame(ALBaseContext* ctx,
                            MppData *src_data)
```

## MPI API

```
S32 VDEC_Init(MppVdecCtx *ctx);
```

## other

### if

```
if (xxxxxxxxx &&
        xxxxxxxxxxxxx &&
        xxxxxxxxxxxxx) {

} else if () {

} else {

}
```
