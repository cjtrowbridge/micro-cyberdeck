#!/bin/bash

if [ -z "$ACUITY_PATH" ]; then
    echo "Need to set environment variable ACUITY_PATH"
        exit 1
fi

PEGASUS=$ACUITY_PATH/pegasus 
if [ ! -e "$PEGASUS" ]; then
    PEGASUS="python3 $PEGASUS.py"
fi 

function rm_json_data()
{
    if [ -f ${NAME}.json ]; then
        echo -e "\033[31m rm  ${NAME}.json \033[0m" 
        rm ${NAME}.json
    fi
    
    if [ -f ${NAME}.data ]; then
        echo -e "\033[31m rm  ${NAME}.data \033[0m" 
        rm ${NAME}.data
    fi 
}
function import_caffe_network()
{
    NAME=$1    
    rm_json_data
    
    echo "=========== Converting $NAME Caffe model ==========="
    if [ -f ${NAME}.caffemodel ]; then
    cmd="$PEGASUS import caffe\
        --model         ${NAME}.prototxt \
        --weights       ${NAME}.caffemodel \
        --output-model  ${NAME}.json \
        --output-data   ${NAME}.data"
    else
    echo "=========== fake Caffe model data file==========="
    cmd="$PEGASUS import caffe\
        --model         ${NAME}.prototxt \
        --output-model  ${NAME}.json \
        --output-data   ${NAME}.data"
    fi  
}

function import_tensorflow_network()
{
    NAME=$1
    rm_json_data 
    
    echo "=========== Converting $NAME Tensorflow model ==========="
    cmd="$PEGASUS import tensorflow\
        --model         ${NAME}.pb \
        --output-model  ${NAME}.json \
        --output-data   ${NAME}.data \
        $(cat inputs_outputs.txt)"
}

function import_pytorch_network()
{
    NAME=$1
    rm_json_data 
    
    echo "=========== Converting $NAME Pytorch model ==========="
    cmd="$PEGASUS import pytorch\
        --model         ${NAME}.pt \
        --output-model  ${NAME}.json \
        --output-data   ${NAME}.data \
        $(cat inputs_outputs.txt)"
}

function import_keras_network()
{
    NAME=$1
    rm_json_data 
    
    echo "=========== Converting $NAME Keras model ==========="
    cmd="$PEGASUS import keras\
        --model         ${NAME}.h5 \
        --output-model  ${NAME}.json \
        --output-data   ${NAME}.data \
        $(cat inputs_outputs.txt)"
}

function import_onnx_network()
{
    NAME=$1
    rm_json_data
    
    echo "=========== Converting $NAME ONNX model ==========="
    cmd="$PEGASUS import onnx\
        --model         ${NAME}.onnx \
        --output-model  ${NAME}.json \
        --output-data   ${NAME}.data \
        $(cat inputs_outputs.txt)"

}

function import_tflite_network()
{
    NAME=$1
    rm_json_data  
    
    echo "=========== Converting $NAME TFLite model ==========="
    cmd="$PEGASUS import tflite\
        --model         ${NAME}.tflite \
        --output-model  ${NAME}.json \
        --output-data   ${NAME}.data" 
}

function import_darknet_network()
{
    NAME=$1
    rm_json_data
    
    echo "=========== Converting $NAME darknet model ==========="
    cmd="$PEGASUS import darknet\
        --model         ${NAME}.cfg \
        --weights       ${NAME}.weights \
        --output-model  ${NAME}.json \
        --output-data   ${NAME}.data"
}
function generate_inputmeta()
{
    NAME=$1 
    
    echo "=========== Generate $NAME inputmeta file ==========="
    cmd="$PEGASUS generate inputmeta\
        --model               ${NAME}.json \
        --separated-database \
        --input-meta-output   ${NAME}_inputmeta.yml"
}

function modify_inputmeta()
{
    MY_DIR=$(dirname "$0")
    NAME=$1 

    if [ -f channel_mean_value.txt ]; then
        cmd="python3 ${MY_DIR}/awnet_normalize.py \
            ${NAME} \
            channel_mean_value.txt"
        echo $cmd
        eval $cmd
    fi
}

function generate_postprocess_file()
{
    NAME=$1 
    
    echo "=========== Generate $NAME postprocess_file file ==========="
    cmd="$PEGASUS generate postprocess-file \
        --model               ${NAME}.json \
        --postprocess-file-output    ${NAME}_postprocess_file.yml"
}

function import_network()
{
    NAME=$1
    pushd $NAME
     
    if [ -f ${NAME}.prototxt ]; then
        import_caffe_network ${1%/}
    elif [ -f ${NAME}.pb ]; then
        import_tensorflow_network ${1%/}
	elif [ -f ${NAME}.pt ]; then
        import_pytorch_network ${1%/}
    elif [ -f ${NAME}.h5 ]; then
        import_keras_network ${1%/}
    elif [ -f ${NAME}.onnx ]; then
        import_onnx_network ${1%/}
    elif [ -f ${NAME}.tflite ]; then
        import_tflite_network ${1%/}
    elif [ -f ${NAME}.weights ]; then
        import_darknet_network ${1%/}
    else
        echo "=========== can not find suitable model files ==========="
    fi

    echo $cmd
    eval $cmd
    
    if [ -f ${NAME}.data -a -f ${NAME}.json ]; then
        echo -e "\033[31m import SUCCESS \033[0m" 
            
        if [ -f ${NAME}_inputmeta.yml ]; then
            echo -e "\033[31m already has ${NAME}_inputmeta.yml \033[0m" 
        else
            generate_inputmeta ${1%/}
            echo $cmd
            eval $cmd
            echo -e "\033[31m generate NAME inputmeta ! \033[0m" 
            echo -e "\033[31m pls modify the contents of ${NAME}_inputmeta.yml ! \033[0m" 
            modify_inputmeta ${1%/}
        fi 
        
        if [ -f ${NAME}_postprocess_file.yml ]; then
            echo -e "\033[31m already has ${NAME}_postprocess_file.yml \033[0m" 
        else
            generate_postprocess_file ${1%/}
            echo $cmd
            eval $cmd
            echo -e "\033[31m generate NAME postprocess_file ! \033[0m" 
            echo -e "\033[31m pls modify the contents of ${NAME}_postprocess_file.yml ! \033[0m" 
        fi 
		
		if [ -f ${NAME}.quantize ]; then
		    echo -e "\033[31m ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ \033[0m" 
            echo -e "\033[31m !!! it's a quant model, in order to suit the naming rule , rename to ${NAME}_uint8.quantize !!! \033[0m" 
			echo -e "\033[31m ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ \033[0m" 
			mv ${NAME}.quantize ${NAME}_uint8.quantize
		fi
    else
        echo -e "\033[31m import model ERROR ! \033[0m" 
    fi  
    popd
}

if [ "$#" -ne 1 ]; then
    echo "Enter a network name !"
    exit -1
fi

import_network ${1%/}
