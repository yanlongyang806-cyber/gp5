$(document).ready(function(){
    $.beautyOfCode.init({brushes: ['Xml']});

    $('a').click(function(){
        $(this).closest('h3').next('pre').toggle();
        $(this).closest('h3').next('div').toggle();
        $(this).closest('h3').next('pre').beautifyCode('xml');
        return false;
    })
});
