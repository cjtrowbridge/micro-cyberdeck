#!/bin/bash

if [ -z "$ACUITY_PATH" ]; then
    echo "Need to set environment variable ACUITY_PATH"
    exit 1
fi

PEGASUS=$ACUITY_PATH/pegasus 
if [ ! -e "$PEGASUS" ]; then
    PEGASUS="python3 $PEGASUS.py"
fi  

function quantize_network()
{
    NAME=$1
    pushd $NAME
    
    QUANTIZED=$2
    POSTFIX=$2
    if [ ${QUANTIZED} = 'float' ]; then 
        echo "=========== do not need quantied, pls change the 2rd param like ( uint8 / int16 / bf16 / pcq )============"
        exit -1 
    elif [ ${QUANTIZED} = 'uint8' ]; then 
        QUANTIZER="asymmetric_affine"       
    elif [ ${QUANTIZED} = 'int16' ]; then  
        QUANTIZER="dynamic_fixed_point" 
    elif [ ${QUANTIZED} = 'pcq' ]; then  
        QUANTIZER="perchannel_symmetric_affine" 
        QUANTIZED="int8"
        POSTFIX="pcq"
    elif [ ${QUANTIZED} = 'bf16' ]; then  
        QUANTIZER="qbfloat16" 
        QUANTIZED="qbfloat16"
        POSTFIX="bf16"
    else
        echo "=========== wrong quantization_type ! ( uint8 / int16 / bf16 / pcq )==========="
        exit -1 
    fi

    echo " ======================================================================="
    echo " ==== Start Quantizing $NAME model with type of ${quantization_type} ==="
    echo " ======================================================================="
    
    if [ -f ${NAME}_${POSTFIX}.quantize ]; then
        echo -e "\033[31m rm  ${NAME}_${POSTFIX}.quantize \033[0m" 
        rm ${NAME}_${POSTFIX}.quantize
    fi  
    
    cmd="$PEGASUS quantize \
        --model         ${NAME}.json \
        --model-data    ${NAME}.data \
        --device        CPU \
        --with-input-meta ${NAME}_inputmeta.yml \
		--compute-entropy \
        --rebuild  \
        --model-quantize  ${NAME}_${POSTFIX}.quantize \
        --quantizer ${QUANTIZER} \
        --qtype  ${QUANTIZED} "
    
    echo $cmd
    eval $cmd
    
    if [ -f ${NAME}_${POSTFIX}.quantize ]; then
        echo -e "\033[31m SUCCESS \033[0m" 
    else
        echo -e "\033[31m ERROR ! \033[0m" 
    fi  
    
    popd
}

if [ "$#" -lt 2 ]; then
    echo "Enter a network name and quantized type ( uint8 / int16 / bf16 / pcq  )"
    exit -1
fi

quantize_network ${1%/} ${2%/}