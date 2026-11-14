// A page owns validation and persistence. The host only coordinates dirty guards.
export function settingsFactory(ctx, page, field, fallback) {
    return function(view) {
        let baseline, value=fallback, loading=true;
        function status(text) {view.set("status",{text:text});}
        function read(done) {
            loading=true;
            view.config.read(function(error,snapshot) {
                if(error){loading=false;status(error.message);if(done)done(error);return;}
                const all=snapshot.exists?snapshot.value:{};
                if(!all||typeof all!=="object"||Array.isArray(all)){loading=false;const error={code:"INVALID_ARGUMENT",message:"配置格式无效，已保留原值"};status(error.message);if(done)done(error);return;}
                const data=page?(all[page]||{}):all;
                if(!data||typeof data!=="object"||Array.isArray(data)){loading=false;const error={code:"INVALID_ARGUMENT",message:"页面配置格式无效，已保留原值"};status(error.message);if(done)done(error);return;}
                baseline=data[field];value=typeof baseline==="string"?baseline:fallback;
                view.set("input",{text:value},function(error){loading=false;if(!error)view.setDirty(false);status(error?error.message:"已加载");if(done)done(error);});
            });
        }
        view.on("input","textChanged",function(text){value=text;if(!loading)view.setDirty(true);});
        function save(done) {
            const savedValue=value;
            if(!value.trim()){const error={code:"INVALID_ARGUMENT",message:"内容不能为空"};status(error.message);done(error);return;}
            view.config.read(function(error,snapshot) {
                if(error){status(error.message);done(error);return;}
                let all=snapshot.exists?snapshot.value:{};
                if(!all||typeof all!=="object"||Array.isArray(all)){done({code:"INVALID_ARGUMENT",message:"配置格式无效，已保留原值"});return;}
                const current=page?(all[page]||{}):all;
                if(current[field]!==baseline){const conflict={code:"CONFLICT",message:"本页配置已被其他任务修改，请放弃并重新加载后合并"};status(conflict.message);done(conflict);return;}
                let candidate=Object.assign({},current);candidate[field]=savedValue;
                if(page){all=Object.assign({},all);all[page]=candidate;}else all=candidate;
                view.config.commit(snapshot.revision,all,function(error){if(!error){baseline=savedValue;status("已保存");}else status(error.message);done(error);});
            });
        }
        view.on("save","clicked",function(){view.save();});read();
        return {save:save,discard:function(done){read(done);}};
    };
}
