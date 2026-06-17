/*
 *
 * jquery.sound.js
 * (c) Vipin Kumar Rajput
 *
 */
(function($) {
    //
    // plugin definition
    //
    var status = "stopped";
    $.fn.soundPlay = function(options) {
        
        // build main options before element iteration
        var opts = $.extend({}, $.fn.soundPlay.defaults, options);
        // iterate and reformat each matched element
        if(opts.command == "play") {
            if(status == "stopped") {
                $.fn.soundPlay.play(opts);
                status = "playing";
            }
        } else if(opts.command == "stop") {
            $.fn.soundPlay.stop(opts);
            status = "stopped";
        } 
    };

    function debug($obj) {
        if (window.console && window.console.log)
            window.console.log('soundPlay: ' + $obj.size());
    };

    $.fn.soundPlay.play = function(opts) {
        url = opts.url;
        id = opts.playerId;
        $("body").append('<embed id="' + id + '" src="'+url+'" autostart="true" hidden="true"></embed>');
        return false;
    };
    $.fn.soundPlay.stop = function(opts) {
        $('#'+opts.playerId).remove();
        return false;
    };
    //
    // plugin defaults
    //
    $.fn.soundPlay.defaults = {
        url: '',
        playerId: 'player',
        command: 'play'
    };

})(jQuery);
