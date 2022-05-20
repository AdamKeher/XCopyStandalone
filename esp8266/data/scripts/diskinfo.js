function drawDisk(x, y, r) {
    let c = document.getElementById("diskCanvas");
    let ctx = c.getContext("2d");

    // magnetic surface
    ctx.beginPath();
    ctx.arc(x, y, r, 0, 2 * Math.PI);
    let gradient = ctx.createLinearGradient(0, 0, r, 0);
    gradient.addColorStop("0", "#121212");
    gradient.addColorStop("0.5" ,"#101010");
    gradient.addColorStop("1.0", "#222222");
    ctx.strokeStyle = gradient;
    ctx.fillStyle = "#121212";
    ctx.lineWidth = 5;
    ctx.fill();
    ctx.stroke();

    // guard
    ctx.beginPath();
    ctx.arc(x, y, r / 2.5, 0, 2 * Math.PI);
    ctx.lineWidth = r / 20;
    ctx.strokeStyle = "#000000";
    ctx.fillStyle = "#303030";
    ctx.fill();
    ctx.stroke();

    // hub
    ctx.beginPath();
    ctx.arc(x, y, r / 4, 0, 2 * Math.PI);
    ctx.lineWidth = 2;
    ctx.strokeStyle = "#000000";
    ctx.fillStyle = "#FFFFFF";
    ctx.fill();
    ctx.stroke();

    // index
    ctx.beginPath();
    ctx.arc(x + (r / 3.175), y, r / 40, 0, 2 * Math.PI);
    ctx.lineWidth = 2;
    ctx.strokeStyle = "#000000";
    ctx.fillStyle = "#FFFFFF";
    ctx.fill();
    ctx.stroke();
}

function onLoad_DiskInfo() {
    let r = 200;
    let padding = 25;

    let x = r + padding;
    let y = r + padding;

    drawDisk(x, y, r);
    
    x = x + r + padding + r;
    drawDisk(x, y, r);
}
