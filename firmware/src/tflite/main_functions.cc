/*
 * Copyright 2019 The TensorFlow Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "main_functions.h"
#include "magic_wand_model_data.h"
#include <tensorflow/lite/micro/micro_error_reporter.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/micro/micro_mutable_op_resolver.h>
#include <tensorflow/lite/schema/schema_generated.h>

float output=0;
/* Globals, used for compatibility with Arduino-style sketches. */
namespace {
	tflite::ErrorReporter *error_reporter = nullptr;
	const tflite::Model *model = nullptr;
	tflite::MicroInterpreter *interpreter = nullptr;
	TfLiteTensor *model_input = nullptr;
	int input_length;

	/* Create an area of memory to use for input, output, and intermediate arrays.
	* The size of this will depend on the model you're using, and may need to be
	* determined by experimentation.
	*/
	constexpr int kTensorArenaSize = 60 * 1024;
	uint8_t tensor_arena[kTensorArenaSize];
} /* namespace */

/* The name of this function is important for Arduino compatibility. */
void setup_tflite(void)
{
	/* Set up logging. Google style is to avoid globals or statics because of
	 * lifetime uncertainty, but since this has a trivial destructor it's okay.
	 */
	static tflite::MicroErrorReporter micro_error_reporter; /* NOLINT */

	error_reporter = &micro_error_reporter;

	/* Map the model into a usable data structure. This doesn't involve any
	 * copying or parsing, it's a very lightweight operation.
	 */
	model = tflite::GetModel(g_magic_wand_model_data);
	if (model->version() != TFLITE_SCHEMA_VERSION) {
		TF_LITE_REPORT_ERROR(error_reporter,
				     "Model provided is schema version %d not equal "
				     "to supported version %d.",
				     model->version(), TFLITE_SCHEMA_VERSION);
		return;
	}

	/* Pull in only the operation implementations we need.
	 * This relies on a complete list of all the ops needed by this graph.
	 * An easier approach is to just use the AllOpsResolver, but this will
	 * incur some penalty in code space for op implementations that are not
	 * needed by this graph.
	 */
	static tflite::MicroMutableOpResolver < 10 > micro_op_resolver; /* NOLINT */
	micro_op_resolver.AddReshape();
	micro_op_resolver.AddConv2D();
	micro_op_resolver.AddDepthwiseConv2D();
	micro_op_resolver.AddFullyConnected();
	micro_op_resolver.AddMaxPool2D();
	micro_op_resolver.AddSoftmax();
    micro_op_resolver.AddQuantize();
    micro_op_resolver.AddSub();
    micro_op_resolver.AddLogistic();
    micro_op_resolver.AddDequantize();

	/* Build an interpreter to run the model with. */
	static tflite::MicroInterpreter static_interpreter(
		model, micro_op_resolver, tensor_arena, kTensorArenaSize, error_reporter);
	interpreter = &static_interpreter;

	/* Allocate memory from the tensor_arena for the model's tensors. */
    interpreter->AllocateTensors();

     //Obtain pointer to the model's input tensor. 
    model_input = interpreter->input(0);
    if ((model_input->dims->size != 4) || (model_input->dims->data[0] != 1) ||
        (model_input->dims->data[1] != 32) ||
        (model_input->dims->data[2] != 13) ||
        (model_input->type != kTfLiteFloat32)) {
        TF_LITE_REPORT_ERROR(error_reporter,
                     "Bad input tensor parameters in model");
        return;
    }

    input_length = model_input->bytes / sizeof(float);
}

void loop(float detection_buffer[], int buff_size)
{
    
    for (int i=0; i<buff_size; i++){
        model_input->data.f[i]=detection_buffer[i];
    }

	/* Run inference, and report any error */
	TfLiteStatus invoke_status = interpreter->Invoke();
	if (invoke_status != kTfLiteOk) {
		TF_LITE_REPORT_ERROR(error_reporter, "Invoke failed");
		return;
	}

	/* Analyze the results to obtain a prediction */
    //int gesture_index = PredictGesture(interpreter->output(0)->data.f);
    output = interpreter->output(0)->data.f[0];


}
