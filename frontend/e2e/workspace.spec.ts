import { test, expect } from "@playwright/test";

const reverse = `#include <pthread.h>
int main(void) {
 pthread_mutex_t x,y; pthread_mutex_init(&x,0); pthread_mutex_init(&y,0);
 pthread_mutex_lock(&x); pthread_mutex_lock(&y);
 pthread_mutex_unlock(&y); pthread_mutex_unlock(&x);
 pthread_mutex_lock(&y); pthread_mutex_lock(&x);
 pthread_mutex_unlock(&x); pthread_mutex_unlock(&y);
 pthread_mutex_destroy(&x); pthread_mutex_destroy(&y);
 return 0;
}`;

test("user paste build run inspect edit rebuild upload and replay use real processes", async ({page,request})=>{
  test.setTimeout(120000);
  const capability=await request.get('http://127.0.0.1:8000/api/v1/capabilities');
  expect(capability.ok()).toBeTruthy();
  const caps=await capability.json();
  expect(caps.isolation.available, caps.isolation.reason).toBeTruthy();
  const errors:string[]=[];page.on('pageerror',e=>errors.push(e.message));
  await page.goto('/#view=yourcode');
  await expect(page.getByRole('button',{name:'Build',exact:true})).toBeEnabled();
  await page.getByLabel('C source editor').fill(reverse);
  await page.getByRole('button',{name:'Build',exact:true}).click();
  await expect(page.getByText('Build succeeded',{exact:true})).toBeVisible({timeout:30000});
  await page.screenshot({path:'test-results/user-code-built.png',fullPage:true});
  await page.getByRole('button',{name:'Run with Monitoring',exact:true}).click();
  await expect(page.locator('.session-strip .badge')).toHaveText('completed',{timeout:15000});
  await expect(page.locator('.react-flow__node')).toHaveCount(2);
  await expect(page.locator('.react-flow__edge')).toHaveCount(2);
  const first=new URLSearchParams(new URL(page.url()).hash.slice(1)).get('session')!;
  const s=await (await request.get(`http://127.0.0.1:8000/api/v1/sessions/${first}`)).json();
  expect(s.warning_count).toBe(1);expect(s.target_kind).toBe('user_build');
  expect(s.build_metadata.files[0].content).toBe(reverse);
  await page.getByRole('button',{name:'Reports',exact:true}).click();
  const download=page.waitForEvent('download');await page.getByRole('button',{name:'Export JSON'}).click();
  const file=await download;expect(file.suggestedFilename()).toContain(first);
  await page.getByRole('button',{name:'Your Code',exact:true}).click();
  await page.getByLabel('C source editor').fill('#include "answer.h"\nint main(){return answer()!=42;}\n');
  await expect(page.getByText(/STALE: source edited/)).toBeVisible();
  await expect(page.getByRole('button',{name:'Run with Monitoring',exact:true})).toBeDisabled();
  await page.getByLabel('Upload C/H files').setInputFiles([
    {name:'answer.h',mimeType:'text/plain',buffer:Buffer.from('int answer(void);')},
    {name:'answer.c',mimeType:'text/plain',buffer:Buffer.from('int answer(void){return 42;}')}
  ]);
  await expect(page.getByRole('tab',{name:'answer.c',exact:true})).toBeVisible();
  await page.getByRole('button',{name:'Build',exact:true}).click();
  await expect(page.getByText('Build succeeded',{exact:true})).toBeVisible({timeout:30000});
  await page.getByRole('button',{name:'Run with Monitoring',exact:true}).click();
  await expect(page.locator('.session-strip .badge')).toHaveText('completed');
  await expect(page.getByText(/No relevant mutex activity observed/)).toBeVisible();
  const second=new URLSearchParams(new URL(page.url()).hash.slice(1)).get('session')!;
  expect(second).not.toBe(first);
  const before=await (await request.get('http://127.0.0.1:8000/api/v1/sessions')).json();
  await page.locator('.session-strip').getByRole('button',{name:'Replay',exact:true}).click();
  await expect(page.getByRole('heading',{name:'Recorded Session Replay',exact:true,level:1})).toBeVisible();
  const after=await (await request.get('http://127.0.0.1:8000/api/v1/sessions')).json();
  expect(after.length).toBe(before.length);
  await page.reload();await expect(page.locator('.session-strip .badge')).toHaveText('completed');
  await page.screenshot({path:'test-results/user-code-replay.png',fullPage:true});
  expect(errors).toEqual([]);
});

test("source editor renders pasted HTML as text",async({page})=>{
  await page.goto('/#view=yourcode');
  await page.getByLabel('C source editor').fill('/* <img src=x onerror="alert(1)"> */\nint main(){return 0;}');
  await expect(page.locator('.source-editor img')).toHaveCount(0);
});
