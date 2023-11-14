# C naming Conventions

## varible

 - b:bool
 - p:pointer
 - n:int
 - l:long int
 - f:float
 - d:double
 - c:char
 - s:string
 - st:struct
 - u:union
 - e:enum

### local varible

```
S32 total_num;
S32 tmp;
S32 i;
```

### global varible

```
S32 nTotalNum;
U8  *pDataPointer;
```

### struct

```
typedef struct _ALSoftOpenh264EncContext ALSoftOpenh264EncContext;

struct _ALSoftOpenh264EncContext {
    ALBaseContext stAlBaseContext;
    ISVCEncoder *pSvcEncoder;
    SEncParamBase stParam;
    S32 bResult;
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
if(xxxxxxxxx &&
        xxxxxxxxxxxxx &&
        xxxxxxxxxxxxx)
{

}
else if()
{

}
else
{

}
```
