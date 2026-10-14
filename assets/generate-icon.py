from pathlib import Path
from PIL import Image,ImageDraw
import math
r=Path('light-timer');a=r/'assets'
svg='''<svg xmlns="http://www.w3.org/2000/svg" width="128" height="128" viewBox="0 0 128 128">
<title>轻计时</title>
<rect x="7" y="7" width="114" height="114" rx="27" fill="#242424" stroke="#555555" stroke-width="1"/>
<g fill="none" stroke="#fafafa" stroke-linecap="round" stroke-linejoin="round">
<path d="M90.046 48.145 A34 34 0 1 1 64 36" stroke-width="8"/>
<path d="M64 70L80 50" stroke-width="8"/>
<path d="M64 27V35" stroke-width="6"/>
<path d="M54 23H74" stroke-width="8"/>
</g></svg>'''
(a/'app-icon.svg').write_text(svg,encoding='utf-8')
def render(size):
 scale=8;canvas=size*scale;k=canvas/128
 im=Image.new('RGBA',(canvas,canvas));d=ImageDraw.Draw(im)
 box=lambda vals:tuple(round(v*k) for v in vals)
 d.rounded_rectangle(box((7,7,121,121)),radius=round(27*k),fill='#242424',outline='#555555',width=max(1,round(k)))
 white='#fafafa'
 def dot(x,y,r):d.ellipse(box((x-r,y-r,x+r,y+r)),fill=white)
 def line(points,width):
  d.line([tuple(round(v*k) for v in q) for q in points],fill=white,width=round(width*k))
  for x,y in [points[0],points[-1]]:dot(x,y,width/2)
 # Center the 8-unit stroke on the SVG radius of 34.
 d.arc(box((26,32,102,108)),start=-40,end=270,fill=white,width=round(8*k))
 for angle in [-40,270]:dot(64+34*math.cos(math.radians(angle)),70+34*math.sin(math.radians(angle)),4)
 line([(64,70),(80,50)],8);line([(64,27),(64,35)],6);line([(54,23),(74,23)],8)
 return im.resize((size,size),Image.Resampling.LANCZOS)
frames=[render(n) for n in [16,20,24,32,40,48,64,128,256]]
frames[-1].save(a/'app-icon.png')
frames[-1].save(a/'app.ico',format='ICO',append_images=frames[:-1],sizes=[(n,n) for n in [16,20,24,32,40,48,64,128,256]])
# Preview the actual small frames against light and dark backgrounds.
preview=Image.new('RGB',(360,150),'#eeeeee');pd=ImageDraw.Draw(preview);pd.rectangle((0,75,360,150),fill='#181818')
for y in [0,75]:
 for n,x in [(16,24),(24,80),(32,145),(48,230)]:
  im=render(n);preview.paste(im,(x,y+(75-n)//2),im)
preview.save(r/'scratch/icon/sizes.png')
print('Created SVG, PNG, and 9-size ICO.')
