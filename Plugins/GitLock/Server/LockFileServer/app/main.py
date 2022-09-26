import json
from typing import Union,Any,Dict,AnyStr

from fastapi import FastAPI
from pydantic import BaseModel
from databases import Database


app = FastAPI()

class Item(BaseModel):
    item_id:str
    locked: Union[bool, None] = None
    user:str
    branch:str


database = Database("sqlite:///data/data.db")

# LockData
async def get_lock_data():
    query = "SELECT item_id, locked, user, branch FROM lock_data WHERE locked=1"
    sqlresult = await database.fetch_all(query=query)
    result = {}
    for item in sqlresult:
        result[item['item_id']]={'user':item['user'],'branch':item['branch']}
    return result

async def set_lock_data(item_id,locked,user,branch):
    query = "REPLACE INTO lock_data(item_id, locked, user,branch) VALUES ('{}', {}, '{}', '{}')".format(item_id,locked,user,branch)
    await database.execute(query=query)
    
async def clean_lock_data(user):
    query = "UPDATE lock_data set locked = 0 where user='{}'".format(user)
    await database.execute(query=query)
    

# ChangedData
async def get_changed_data_id(hash):
    query = "SELECT id FROM changed_data WHERE hash='{}'".format(hash)
    sqlresult = await database.fetch_all(query=query)
    result = -1
    for item in sqlresult:
        result=item['id']
    return result

async def get_changed_data_last(num):
    query = "SELECT * FROM changed_data ORDER BY id DESC LIMIT {}".format(num)
    sqlresult = await database.fetch_all(query=query)
    result = []
    for item in sqlresult:
        single={}
        js = item['list']
        data = json.loads(js)
        single['id'] = item['id']
        single['user'] = item['user']
        single['hash'] = item['hash']
        single['branch'] = item['branch']
        single['list'] = data
        result.append(single)
    return result

async def get_changed_data_changelist(id):
    query = "SELECT * FROM changed_data WHERE id > {}".format(id)
    sqlresult = await database.fetch_all(query=query)
    result = []
    for item in sqlresult:
        single={}
        js = item['list']
        data = json.loads(js)
        single['id'] = item['id']
        single['user'] = item['user']
        single['hash'] = item['hash']
        single['branch'] = item['branch']
        single['list'] = data
        result.append(single)
    return result

async def set_changed_data(branch,hash,user,listdata):
    jsdata = json.dumps(listdata)
    query = "INSERT INTO changed_data (hash,user,list,branch) VALUES ('{}', '{}', '{}','{}')".format(hash,user,jsdata,branch)
    await database.execute(query=query)


@app.on_event("startup")
async def database_connect():
    await database.connect()


@app.on_event("shutdown")
async def database_disconnect():
    await database.disconnect()



@app.get("/isready")
async def isready():
    return {"isready":True}
    

@app.get("/getlock/{item_id}")
async def getlock(item_id: str):
    lock_data = await get_lock_data()
    if(item_id in lock_data):
        item = lock_data[item_id]
        return {"item_id": item_id, "locked": True,"user": item['user'],"branch": item['branch']}
    else:
        return {"item_id": item_id, "locked": False,"user": None, "branch": None}


@app.get("/getalllock")
async def getalllock():
    result = await get_lock_data()
    return result


@app.get("/getallchanged")
async def getallchanged():
    result = await get_changed_data_last(50)
    return result


@app.post("/setlock/{hash}")
async def setlock(hash:str, item: Item):
    lock_data = await get_lock_data()
    print("SetLock {} ---by {}  ---in {}:".format(item.item_id,item.user,item.branch))
    if(item.locked):
        if(item.item_id in lock_data):
            sqlitem = lock_data[item.item_id]
            result = {"item_id": item.item_id, "locked": True, "user": sqlitem['user'],"branch": sqlitem['branch'],"committed": False,"result": False}
            print(result)
            return result

        index = await get_changed_data_id(hash)
        changelist=[]
        if(index > 0):
            changelist = await get_changed_data_changelist(index)
        
        for change in changelist:
            if item.item_id in change['list']:
                result = {"item_id": item.item_id, "locked": False, "user":change['name'],"branch": change['branch'],"committed": True,"result": False}
                print(result)
                return result

        await set_lock_data(item.item_id,int(item.locked),item.user,item.branch)
        result = {"item_id": item.item_id, "locked": True, "user": item.user,"branch": item.branch,"committed": False,"result": True}
        print(result)
        return result
    else:
        if(item.item_id in lock_data):
            await set_lock_data(item.item_id,int(item.locked),item.user)
        result = {"item_id": item.item_id, "locked": False, "user": item.user,"branch":item.branch,"committed": False,"result": True}
        print(result)
        return result


@app.post("/cleanlock/{user}")
async def cleanlock(user: str):
    await clean_lock_data(user)
    return {"result": True}


@app.post("/gitlabpush")
async def gitlabpush(data : Dict[AnyStr, Any]):
    branch = data[b'ref']
    a = branch.split('/')
    branch = a[len(a)-1]
    user_name = data[b'user_name']
    user_username = data[b'user_username']
    print("user_name:"+user_name)
    print("user_username:"+user_username)

    commits = data[b'commits']
    for commit in commits:
        hash = commit['id']
        author = commit['author']
        modified = commit['modified']
        removed = commit['removed']
        await set_changed_data(branch,hash,author['name'],modified+removed)
    
    user = user_username
    await clean_lock_data(user)
    return {"result": True}


import uvicorn
if __name__ == '__main__':
    uvicorn.run(app)