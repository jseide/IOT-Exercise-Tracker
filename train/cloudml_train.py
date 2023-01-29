import json
import os
import numpy as np
import tensorflow as tf
from data_prepare import data_cloudml_prepare

DATA_DIM = 13
PADDED_LENGTH = 100
LABEL_NAME = "label"
DATA_NAME = "data"
DATA_PATH = "data/cloud"

def get_data_file(dirpath):
    """Get train, valid and test data from files."""
    with open(os.path.join(DATA_PATH,dirpath), "r") as f:
      lines = f.readlines()
    labels=np.array([int(json.loads(dic)[LABEL_NAME]) for dic in lines])
    data=np.array([json.loads(dic)[DATA_NAME] for dic in lines])
    data=tf.keras.preprocessing.sequence.pad_sequences(data, maxlen=100, padding='post',dtype='float64')
    dataset = tf.data.Dataset.from_tensor_slices((data, labels.astype("int32")))
    return dataset.batch(10)

def load_data():
    train_data = get_data_file('train')
    valid_data = get_data_file('valid')
    test_data = get_data_file('test')
    return train_data,valid_data,test_data

def build_net():
    """Builds an LSTM in Keras."""
    model = tf.keras.Sequential([
    tf.keras.layers.Masking(input_shape=(PADDED_LENGTH, DATA_DIM)),
    tf.keras.layers.Bidirectional(tf.keras.layers.LSTM(24)),
    tf.keras.layers.Dense(4, activation='relu'),
    tf.keras.layers.Dense(1)
    ])
    return model 

if __name__=='__main__':

    data_cloudml_prepare()
    train_data, valid_data, test_data = load_data()

    model = build_net()
    model.compile(loss=tf.keras.losses.BinaryCrossentropy(from_logits=True),
              optimizer=tf.keras.optimizers.Adam(1e-4),
              metrics=['accuracy'])
    
    history = model.fit(train_data, epochs=10,
                    validation_data=valid_data,
                    validation_steps=30)

