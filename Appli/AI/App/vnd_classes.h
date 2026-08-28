/**
  ******************************************************************************
  * @file    vnd_classes.h
  * @brief   Class index -> label mapping for vnd_model (best_int8.onnx).
  *
  * Extracted directly from the ONNX file's embedded metadata (metadata_props
  * "names" entry), which Ultralytics writes from vnd_dataset/data.yaml at
  * export time. The array order below is the class index order the model
  * was actually trained and quantized with -- do not reorder without
  * re-checking the source .onnx, since the model's argmax output is a raw
  * index into this list.
  *
  * These are Vietnamese Dong (VND) banknote denominations.
  ******************************************************************************
  */

#ifndef __VND_CLASSES_H__
#define __VND_CLASSES_H__

#define VND_NUM_CLASSES  9

/* Index order matches the ONNX "names" metadata exactly:
 * {0:'100k',1:'10k',2:'1k',3:'200k',4:'20k',5:'2k',6:'500k',7:'50k',8:'5k'} */
static const char * const vnd_class_names[VND_NUM_CLASSES] =
{
  "100k",   /* 0 -> 100,000 VND */
  "10k",    /* 1 -> 10,000 VND  */
  "1k",     /* 2 -> 1,000 VND   */
  "200k",   /* 3 -> 200,000 VND */
  "20k",    /* 4 -> 20,000 VND  */
  "2k",     /* 5 -> 2,000 VND   */
  "500k",   /* 6 -> 500,000 VND */
  "50k",    /* 7 -> 50,000 VND  */
  "5k"      /* 8 -> 5,000 VND   */
};

#endif /* __VND_CLASSES_H__ */
