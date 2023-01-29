# sudo docker run -it --rm -v $PWD:/tmp -w /tmp tf_gymmy python ./train.py --model CNN
# xxd -i model_quantized.tflite model.cc


import argparse
import datetime
import os
import json

import numpy as np  
import tensorflow as tf

from data_prepare import data_edgeml_prepare

DATA_DIM = 13
SEQ_LENGTH = 32
LABEL_NAME = "label"
DATA_NAME = "data"
DATA_PATH = "data/edge"


def calculate_model_size(model):
    print(model.summary())
    var_sizes = [
        np.product(list(map(int, v.shape))) * v.dtype.size
        for v in model.trainable_variables
    ]
    print("Model size:", sum(var_sizes) / 1024, "KB")


def build_cnn():
    """Builds a convolutional neural network in Keras."""

    normalizer = tf.keras.layers.experimental.preprocessing.Normalization()
    normalizer.adapt(train_data.map(lambda x, y: x))

    input_shape = (SEQ_LENGTH, DATA_DIM, 1)
    model = tf.keras.Sequential([
        tf.keras.Input(shape=input_shape),
        normalizer,
        tf.keras.layers.Conv2D(
            8, (4, 3),
            padding="same",
            activation="relu"),  # output_shape=(batch, 128, 3, 8)
        tf.keras.layers.MaxPool2D((3, 3)),  # (batch, 42, 1, 8)
        tf.keras.layers.Dropout(0.1),  # (batch, 42, 1, 8)
        # tf.keras.layers.Conv2D(16, (4, 1), padding="same",
                               # activation="relu"),  # (batch, 42, 1, 16)
        # tf.keras.layers.MaxPool2D((3, 1),
                                  # padding="same"),  # (batch, 14, 1, 16)
        # tf.keras.layers.Dropout(0.2),  # (batch, 14, 1, 16)
        tf.keras.layers.Flatten(),  # (batch, 224)
        tf.keras.layers.Dense(4, activation="relu"),  # (batch, 16)
        tf.keras.layers.Dropout(0.2),  # (batch, 16)
        tf.keras.layers.Dense(1, activation="sigmoid")  # (batch, 4)
    ])
    return model


def get_data_file(dirpath):
    """Get train, valid and test data from files."""
    with open(os.path.join(DATA_PATH,dirpath), "r") as f:
      lines = f.readlines()
    labels=np.array([int(json.loads(dic)[LABEL_NAME]) for dic in lines])
    data=np.array([json.loads(dic)[DATA_NAME] for dic in lines]).reshape(-1,SEQ_LENGTH,DATA_DIM,1)
    dataset = tf.data.Dataset.from_tensor_slices((data, labels.astype("int32")))
    return dataset


def load_data():
    train_data = get_data_file('train')
    valid_data = get_data_file('valid')
    test_data = get_data_file('test')
    return train_data,valid_data,test_data


def train_net(
        model,
        train_data,
        valid_data,  
        test_data):

    def representative_dataset():
        for data, labels in train_data.unbatch().batch(1).take(100):
            yield [tf.dtypes.cast(data, tf.float32)]

    """Trains the model."""
    calculate_model_size(model)
    epochs = 3
    batch_size = 64
    model.compile(optimizer='adam',
                  loss="binary_crossentropy",
                  metrics=["accuracy"])
    test_labels = np.zeros(len(test_data))
    idx = 0
    for data, label in test_data:  # pylint: disable=unused-variable
        test_labels[idx] = label.numpy()
        idx += 1
    train_data = train_data.batch(batch_size).repeat()
    valid_data = valid_data.batch(batch_size)
    test_data = test_data.batch(batch_size)
    model.fit(train_data,
              epochs=epochs,
              validation_data=valid_data,
              steps_per_epoch=1000,
              validation_steps=int((len(valid_data)- 1) / batch_size + 1),
              # callbacks=[tensorboard_callback]
              )
    loss, acc = model.evaluate(test_data)
    pred = np.round(model.predict(test_data))
    confusion = tf.math.confusion_matrix(labels=tf.constant(test_labels),
                                         predictions=tf.constant(pred),
                                         num_classes=None)

    print(confusion)
    print("Loss {}, Accuracy {}".format(loss, acc))
    # Convert the model to the TensorFlow Lite format without quantization
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    tflite_model = converter.convert()

    # Save the model to disk
    open("model.tflite", "wb").write(tflite_model)

    # Convert the model to the TensorFlow Lite format with quantization
    converter = tf.lite.TFLiteConverter.from_keras_model(model)

    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = representative_dataset
    tflite_model = converter.convert()

    # Save the model to disk
    open("model_quantized.tflite", "wb").write(tflite_model)

    basic_model_size = os.path.getsize("model.tflite")
    print("Basic model is %d bytes" % basic_model_size)
    quantized_model_size = os.path.getsize("model_quantized.tflite")
    print("Quantized model is %d bytes" % quantized_model_size)
    difference = basic_model_size - quantized_model_size
    print("Difference is %d bytes" % difference)


if __name__ == "__main__":

    data_edgeml_prepare()
    train_data, valid_data, test_data = load_data()
    model = build_cnn()
    train_net(model, train_data, valid_data, test_data)

