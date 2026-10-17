#!/bin/bash

if [ -z "$ACUITY_PATH" ]; then
    echo "Need to set environment variable ACUITY_PATH"
        exit 1
fi

PEGASUS=$ACUITY_PATH/pegasus 
if [ ! -e "$PEGASUS" ]; then
    PEGASUS="python3 $PEGASUS.py"
fi   

function inference_network()
{
    NAME=$1
    pushd $NAME
    QUANTIZED=$2
    	
	
	if [ ${QUANTIZED} = 'float' ]; then 
        echo "=========== do not need quantied==========="
        TYPE=float32;
		inf_path='./inf/${NAME}_non-quantized'
    elif [ ${QUANTIZED} = 'uint8' ]; then 
		inf_path='./inf/${NAME}_uint8'
		TYPE=quantized;
    elif [ ${QUANTIZED} = 'int16' ]; then  
		inf_path='./inf/${NAME}_int16'
		TYPE=quantized;
	elif [ ${QUANTIZED} = 'pcq' ]; then  
		inf_path='./inf/${NAME}_pcq'
		TYPE=quantized;
	elif [ ${QUANTIZED} = 'bf16' ]; then  
		inf_path='./inf/${NAME}_bf16'
		TYPE=quantized;
    else
        echo "=========== wrong quantization_type ! (float / bf16 / int16 / uint8 / pcq)==========="
        exit -1 
    fi
	
    
    if [ ${TYPE} = 'float32' ]; then
        echo " ======================================================================="
        echo " =========== Start Inference $NAME model with type of float ============"
        echo " ======================================================================="
        
        cmd="$PEGASUS inference \
        --model         ${NAME}.json \
        --model-data    ${NAME}.data \
        --dtype         ${TYPE} \
		--iterations    1 \
        --device        CPU \
		--output-dir    ${inf_path} \
        --postprocess-file  ${NAME}_postprocess_file.yml \
        --with-input-meta ${NAME}_inputmeta.yml"
        
    else
        echo " ======================================================================="
        echo " = Start Inference $NAME model with type of ${QUANTIZED} ======="
        echo " ======================================================================="
        
        if [ -f ${NAME}_${QUANTIZED}.quantize ]; then
            echo -e "\033[31m using  ${NAME}_${QUANTIZED}.quantize \033[0m" 
        else
            echo -e "\033[31m Can not find  ${NAME}_${QUANTIZED}.quantize \033[0m" 
            exit -1;
        fi  
        
        cmd="$PEGASUS inference \
        --model         ${NAME}.json \
        --model-data    ${NAME}.data \
        --dtype         ${TYPE} \
        --model-quantize ${NAME}_${QUANTIZED}.quantize\
		--iterations    1 \
        --device        CPU \
		--output-dir    ${inf_path} \
        --postprocess-file  ${NAME}_postprocess_file.yml \
        --with-input-meta ${NAME}_inputmeta.yml"
    fi
    
    echo $cmd
    eval $cmd
    echo "=========== End  inference $NAME model  ==========="
    
    popd
}

if [ "$#" -lt 2 ]; then
    echo "Enter a network name and quantized type (float / bf16 / int16 / uint8 / pcq)"
    exit -1
fi

inference_network ${1%/} ${2%/}
