var ajaxUpload
var fileTransferInProgress = false;
var uploadCancelReason = null;

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

function fileUploadCancelClick() {
    if (fileTransferInProgress) {
        fileUploadCancel('aborted');
    } else {
        $('#uploadDetails').hide();
        $('#uploadFile')[0].value = null;
    }
}

function uploadErrorMessage(reason) {
    switch (reason) {
        case 'exists':
            return 'The file you have selected already exists on the SD card.';
        case 'detect':
            return 'The SD card was not detected.';
        case 'init':
            return 'The SD card failed to initialise.';
        case 'open':
            return 'The file failed to open for writing on the SD card.';
        case 'aborted':
            return 'The file upload was aborted by the user. A partial file will be saved to the SD card.';
        case 'timeout':
            return 'The transfer timed out waiting for the XCopy to acknowledge data. The file is incomplete, please retry.';
        case 'short':
            return 'Fewer bytes reached the SD card than expected. The file is incomplete, please retry.';
        case 'write':
            return 'A write to the SD card failed. The file is incomplete, please retry.';
        case 'noreceipt':
            return 'The XCopy did not confirm the transfer. The file may be incomplete, please retry.';
        case 'nosize':
            return 'The file size was not sent with the upload, so the transfer could not be started.';
        default:
            return 'Unknown error.';
    }
}

function fileUploadCancel(reason) {
    uploadCancelReason = reason;

    try {
        ajaxUpload.abort();
    } catch (error) { }

    $('#uploadErrorDetails').html(uploadErrorMessage(reason));
    console.log('Upload aborted: ' + reason);
}

function fileUploadFailed(reason) {
    fileTransferInProgress = false;
    $('#uploadErrorDetails').html(uploadErrorMessage(reason));
    $("#uploadSuccess").hide();
    $("#uploadError").show();
    console.log('Upload failed: ' + reason);
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
    uploadCancelReason = null;

    var fd = new FormData();
    var files = $('#uploadFile')[0].files[0];
    fd.append('file', files);

    ajaxUpload = $.ajax({
        xhr: function() {
            fileTransferInProgress = true;
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
        // Sent as headers rather than form fields: headers are parsed before the request
        // body, so the firmware can read the expected size at UPLOAD_FILE_START without
        // depending on multipart form-argument parsing.
        headers: {
            'X-File-Size': files.size,
            'X-Overwrite': $('#uploadOverwrite').is(':checked') ? '1' : '0'
        },
        data: fd,
        contentType: false,
        processData: false,
        dataType: 'json',
        success: function(response){
            fileTransferInProgress = false;

            // The firmware returns the byte count it actually committed to the SD card plus
            // a CRC32 over that data. A 200 alone is not proof the file arrived intact.
            if (response && response.ok === true) {
                $('#uploadProgress').width('100%');
                $('#uploadCrc32').html(response.crc32);
                $("#uploadSuccess").show();
                $("#uploadError").hide();
                console.log('Upload verified: ' + response.size + ' bytes, crc32 ' + response.crc32);
            }
            else {
                fileUploadFailed(response && response.error ? response.error : 'unknown');
            }
        },
        error: function(jqXHR){
            var reason = uploadCancelReason;
            if (!reason && jqXHR.responseJSON && jqXHR.responseJSON.error) {
                reason = jqXHR.responseJSON.error;
            }
            fileUploadFailed(reason || 'unknown');
        }
    });
}
