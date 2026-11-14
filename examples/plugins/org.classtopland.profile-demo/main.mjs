export function activate(ctx) {
    ctx.actions.register("addSubject",function(args,done){
        if(!args.name.trim()){done({code:"INVALID_ARGUMENT",message:"科目名称不能为空"},null);return;}
        ctx.profiles.getCurrent(function(error,snapshot){
            if(error){done(error,null);return;}
            ctx.profiles.applyChanges({session:snapshot.session,expectedRevision:snapshot.revision,operations:[{op:"subject.add",value:{name:args.name,simplifiedName:"示",teacher:""}}]},function(error,result){done(error,error?null:{revision:result.revision});});
        });
    });
    ctx.ui.registerPage("profile",function(view){
        view.on("addSubject","clicked",function(){view.get("name","text",function(error,name){if(error)return;ctx.actions.invoke("addSubject",{name:name},function(error,result){view.set("status",{text:error?error.code+": "+error.message:"已保存，revision="+result.revision});});});});
        return {};
    });
}
