import collections
import json
import random
import os
import pandas as pd
import firebase_admin
from firebase_admin import credentials, firestore
from itertools import chain

os.makedirs("./data/cloud",exist_ok=True)
os.makedirs("./data/edge",exist_ok=True)

def write_data(data_to_write, path):
    with open(path, "w") as f:
        for idx, item in enumerate(data_to_write):
            dic = json.dumps(item, ensure_ascii=False)
            f.write(dic)
            f.write("\n")

def ingest_firestore(doc_id=None, collection='train'):
    if not firebase_admin._apps:
        cred = credentials.Certificate('path_to_.json')
        firebase_admin.initialize_app(cred)
    db = firestore.client()
    if doc_id:
        return [db.collection(collection).document(doc_id).get()]

    else:
        return db.collection(collection).stream()

def prepare_docs(docs):
    raw_data=[]
    for doc in docs:
        rep={}
        rep_id, rep_count=str(doc.id).split('_')
        rep['label']=rep_count
        rep['id']=rep_id
        rep['data']=collections.OrderedDict(
        sorted(((int(key), value) for key, value in doc.to_dict().items())))
        raw_data.append(rep)
    return raw_data

def generate_cloudml_samples(raw_data):
    data=[]
    thrown_out=0
    for idx, line in enumerate(raw_data):  
        tmp = line['data']
        tmp_ticks = [key for key in tmp.keys()]
        tmp_data = list(tmp.values())
        if (max([t - s for s, t in zip(tmp_ticks, tmp_ticks[1:])])==1):
                rep = {}
                rep['label'] = line['label']
                rep['id'] = line['id']
                rep['data'] = tmp_data
                data.append(rep)
        else:
            # print(line['id'])
            # print(tmp_ticks)
            # print([t - s for s, t in zip(tmp_ticks, tmp_ticks[1:])])
            thrown_out=thrown_out+1
    print(f'thrown_out: {thrown_out}')
    print(f'Dataset length: {len(data)}')
    return data
        
def generate_edgeml_samples(raw_data,seq_length=32):
    data=[]
    thrown_out=0
    for idx, line in enumerate(raw_data):  
        tmp = line['data']
        tmp_ticks = [key for key in tmp.keys()]
        tmp_data = list(tmp.values())
        tmp_data = [
            tmp_data[x:x + seq_length]
            for x in range(0, len(tmp_data), seq_length)
        ]
        tmp_ticks = [
            tmp_ticks[x:x + seq_length]
            for x in range(0, len(tmp_ticks), seq_length)
        ]
        for i, sample in enumerate(tmp_ticks):
            if (len(sample)==seq_length) and (max([t - s for s, t in zip(sample, sample[1:])])==1):
                rep = {}
                rep['label'] = line['label']
                rep['id'] = line['id']
                rep['data'] = tmp_data[i]
                data.append(rep)
            else:
                thrown_out=thrown_out+1
    print(f'thrown_out: {thrown_out}')
    return data

def split_cloudml_data(data, train_ratio, valid_ratio): 
    train_idx = int(train_ratio*len(data))
    valid_idx = int(valid_ratio*len(data))
    train_data = data[:train_idx]
    valid_data = data[train_idx:(train_idx+valid_idx)]
    test_data = data[(train_idx+valid_idx):]
    return train_data, valid_data, test_data

def split_edgeml_data(data, train_ratio, valid_ratio): 
    train_data = []
    valid_data = []
    test_data = []
    num_dic = {'1': 0, '0': 0}
    for idx, item in enumerate(data):
        if item['label'] != '0':
            item['label'] = '1' #plug to disregard rep type
        for i in num_dic:
            if item["label"] == i:
                num_dic[i] += 1
    print('Total counts: {}'.format(num_dic))
    train_num_dic = {}
    valid_num_dic = {}
    for i in num_dic:
        train_num_dic[i] = int(train_ratio * num_dic[i])
        valid_num_dic[i] = int(valid_ratio * num_dic[i])
    random.seed(30)
    random.shuffle(data)
    for idx, item in enumerate(data):
        for i in num_dic:
            if item["label"] == i:
                if train_num_dic[i] > 0:
                    train_data.append(item)
                    train_num_dic[i] -= 1
                elif valid_num_dic[i] > 0:
                    valid_data.append(item)
                    valid_num_dic[i] -= 1
                else:
                    test_data.append(item)
    print("train_length:" + str(len(train_data)))
    print("valid_length:" + str(len(valid_data)))
    print("test_length:" + str(len(test_data)))
    test_label = [item['label'] for item in test_data]
    print(f'Test Breakdown: {collections.Counter(test_label)}')
    return train_data, valid_data, test_data


def data_edgeml_prepare():
    docs = ingest_firestore()
    raw_data = prepare_docs(docs)
    data=generate_edgeml_samples(raw_data)
    train_data, valid_data, test_data = split_edgeml_data(data, 0.5, 0.1)
    write_data(train_data, "./data/edge/train")
    write_data(valid_data, "./data/edge/valid")
    write_data(test_data, "./data/edge/test")

def data_cloudml_prepare():
    docs = ingest_firestore(collection='detect')
    raw_data = prepare_docs(docs)
    data = generate_cloudml_samples(raw_data)
    train_data, valid_data, test_data = split_cloudml_data(data, 0.5, 0.1)
    write_data(train_data, "./data/cloud/train")
    write_data(valid_data, "./data/cloud/valid")
    write_data(test_data, "./data/cloud/test")

def sample_viz(doc_id,**kwargs):
    docs=ingest_firestore(doc_id=doc_id,**kwargs)
    doc=prepare_docs(docs)[0]['data']
    df=pd.DataFrame(doc).T
    df.plot(subplots=True,figsize=(15, 15))


if __name__ == "__main__":
    data_edgeml_prepare()
    data_cloudml_prepare()
