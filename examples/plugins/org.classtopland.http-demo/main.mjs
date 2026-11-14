export function activate(ctx) {
    ctx.actions.register("request",function(args,done,call){
        const request=ctx.http.request({url:"https://example.com",method:"GET"},function(error,response){done(error,error?null:{status:response.status,text:response.body.slice(0,1000)});});
        call.onCancel(function(){request.cancel();});
    });
    ctx.ui.registerPage("network",function(view){
        view.on("request","clicked",function(){ctx.actions.invoke("request",{},function(error,result){view.set("status",{plainText:error?error.code+": "+error.message:result.status+"\n"+result.text});});});
        return {};
    });
}
