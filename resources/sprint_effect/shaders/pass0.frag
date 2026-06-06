#version 300 es
precision highp float;

uniform sampler2D inputTexture0;
uniform sampler2D mask;

uniform float uTime;

in vec2 texCoord;
out vec4 FragColor;

float getFps() { return 12.0; }

float getIntensity() { return 0.0035*2.0; }

vec2 getDirection() {
  vec2 directions[5];
  directions[0] = vec2(-1.0, -1.0);
  directions[1] = vec2(-1.0, 1.0);
  directions[2] = vec2(1.0, 1.0);
  directions[3] = vec2(1.0, -1.0);
  directions[4] = vec2(0.0, 0.0);

  int id = int(floor(getFps() * uTime / 1000.0));
  id = id % 10 + 1;
  if (id > 5) {
    id = 10 - id;
    if (id == 0) {
      id = 5;
    }
  }
  return directions[id] * getIntensity();
}
void main()
{
    vec2 dt = getDirection();
    vec4 srcColor = texture(inputTexture0, texCoord + dt);
    vec2 maskColorUv = vec2(texCoord.x * 0.5, texCoord.y);
    vec2 maskAlphaUv = maskColorUv + vec2(0.5, 0.0);
    vec4 maskColor = vec4(texture(mask, maskColorUv).rgb, texture(mask, maskAlphaUv).r);
    vec3 resultColor = mix(srcColor.rgb, maskColor.rgb, maskColor.a);
    FragColor = vec4(resultColor, 1.0);
}