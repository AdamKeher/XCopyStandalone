var ajaxUpload

function fileUploadChange() {
    if ($('#uploadFile')[0].files.length == 0) {
      $("#uploadSuccess").hide();
      $("#uploadError").hide();
      $("#uploadNoFileError").hide();
      $("#uploadDetails").hide();
      return;
    }
  
    $('#uploadProgress').width('0%').html('0%');
    var files = $('#uploadFile')[0].files[0];
    $('#uploadFilename').html(files.name);
    $('#uploadFileSize').html(files.size);
    $("#uploadSuccess").hide();
    $("#uploadError").hide();
    $("#uploadNoFileError").hide();
    $("#uploadDetails").show();
}

function fileUploadCancel(reason) {
    ajaxUpload.abort();
    if (reason == "exists") {
        $('#uploadErrorDetails').html('The file you have selected already exists on the SD card.');
    }
    else if (reason == "detect") {
        $('#uploadErrorDetails').html('The SD card was not detected.');
    }
    else if (reason == "init") {
        $('#uploadErrorDetails').html('The SD card failed to initialise.');
    }
    else if (reason == "open") {
        $('#uploadErrorDetails').html('The file failed to open for writing on the SD card.');
    }
    else {
        $('#uploadErrorDetails').html('Unknown error.');
    }
    console.log('Upload aborted: ' + reason);
}

function fileUploadSelect() {
    $("#uploadSuccess").hide();
    $("#uploadError").hide();
  
    if ($('#uploadFile')[0].files.length == 0) {
        $("#uploadNoFileError").show();
        $("#uploadDetails").hide();
        return;
    }
  
    $("#uploadNoFileError").hide();
    $("#uploadDetails").show();
    $('#uploadProgress').width('0%');
  
    var fd = new FormData();
    var files = $('#uploadFile')[0].files[0];
    fd.append('file', files);
  
    ajaxUpload = $.ajax({
        xhr: function() {
            $('#uploadErrorDetails').html('Unknown error.');
            var xhr = new window.XMLHttpRequest();
            xhr.upload.addEventListener("progress", function(evt) {
                if (evt.lengthComputable) {
                    var percentComplete = (evt.loaded / evt.total) * 100;
                    $('#uploadProgress').width(percentComplete + '%');
                }
        }, false);
        return xhr;
        },
        url: '/upload?filesize=' + files.size,
        type: 'post',
        data: fd,
        contentType: false,
        processData: false,
        success: function(response){
            if(response != 0){
            $("#uploadSuccess").show();
            $("#uploadError").hide();
            }
            else{
            $("#uploadSuccess").hide();
            $("#uploadError").show();
            }
        },
        error: function(){
        $("#uploadSuccess").hide();
        $("#uploadError").show();
        }
    });
}