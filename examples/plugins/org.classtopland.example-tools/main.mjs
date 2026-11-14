import { settingsFactory } from "./settings.mjs";
export function activate(ctx) {
    ctx.ui.registerPage("general",settingsFactory(ctx,"general","prefix","课程"));
    ctx.ui.registerPage("display",settingsFactory(ctx,"display","caption","今日课程"));
    ctx.ui.registerPage("details",function(view) {
        let notes=0;
        function refresh(snapshot) {
            view.invoke("courses","setItems",{columns:["科目","开始","结束"],items:snapshot.classes.map(function(item){return [item.subject,item.startTime,item.endTime];})});
            view.set("status",{text:snapshot.valid?snapshot.name:snapshot.error});
        }
        ctx.courses.getSnapshot(function(error,snapshot){if(error)view.set("status",{text:error.message});else refresh(snapshot);});
        const subscription=ctx.events.on("courses.tableChanged",refresh);
        view.on("addNote","clicked",function(){const name="note"+(++notes);view.create("root","ElaText",name,function(error){if(error)return;view.set(name,{text:"这是插件动态创建的原生控件"});view.insert("pageLayout",name,{});});});
        return {destroy:function(){subscription.dispose();}};
    });
    ctx.ui.registerComponent("status",{
        create:function(view){
            view.set("label",{text:view.initialSettings.label||"课程"});
            function refresh(snapshot){const index=snapshot.currentClassIndex;view.set("subject",{text:index>=0&&snapshot.classes[index]?snapshot.classes[index].subject:"课间"});view.setAvailable(snapshot.valid);}
            ctx.courses.getSnapshot(function(error,snapshot){if(!error)refresh(snapshot);});
            const subscription=ctx.events.on("courses.stateChanged",refresh);
            return {destroy:function(){subscription.dispose();}};
        },
        createSettings:settingsFactory(ctx,"","label","课程")
    });
    ctx.actions.register("summary",function(args,done){ctx.courses.getSnapshot(function(error,snapshot){done(error,error?null:{text:snapshot.name+"："+snapshot.classes.map(function(item){return item.subject;}).join("、")});});});
}
